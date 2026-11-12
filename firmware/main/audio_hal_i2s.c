#include "config.h"
#include "audio_hal.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "i2s";

static i2s_chan_handle_t s_rx_chan = NULL;   // microphone (INMP441)
static i2s_chan_handle_t s_tx_chan = NULL;   // amplifier (MAX98357A)

#define WRITE_CHUNK 256                      // mono samples expanded per write

static int16_t clamp16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

void audio_hal_init(void)
{
    // ---- Microphone: INMP441 on I2S port 0 (receive) ----
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &s_rx_chan));

    i2s_std_config_t rx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        // The INMP441 sends 24-bit samples inside 32-bit slots, so read 32-bit slots
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_MIC_SCK,
            .ws   = PIN_I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_I2S_MIC_SD,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    // ---- Amplifier: MAX98357A on I2S port 1 (transmit) ----
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear_after_cb = true;   // output silence, not old audio, when nothing is queued
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t tx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        // 16-bit stereo: audio_hal_write sends every mono sample on both slots
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_AMP_BCLK,
            .ws   = PIN_I2S_AMP_LRC,
            .dout = PIN_I2S_AMP_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &tx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    ESP_LOGI(TAG, "I2S ready: microphone on port 0, amplifier on port 1");
}

size_t audio_hal_read(int16_t *out, size_t num_samples)
{
    static int32_t raw[FRAME_SAMPLES];
    static int fail_count = 0;
    size_t total = 0;

    while (total < num_samples) {
        size_t n = num_samples - total;
        if (n > FRAME_SAMPLES) n = FRAME_SAMPLES;

        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(s_rx_chan, raw, n * sizeof(int32_t), &bytes, 1000);
        if (err != ESP_OK) {
            if ((fail_count++ % 50) == 0) {
                ESP_LOGW(TAG, "microphone read failed: %s", esp_err_to_name(err));
            }
            break;
        }
        size_t got = bytes / sizeof(int32_t);
        for (size_t i = 0; i < got; i++) {
            out[total + i] = clamp16(raw[i] >> MIC_SHIFT);   // 32-bit slot -> 16-bit sample
        }
        total += got;
        if (got == 0) break;
    }
    return total;
}

size_t audio_hal_write(const int16_t *in, size_t num_samples)
{
    int16_t stereo[2 * WRITE_CHUNK];
    size_t done = 0;

    while (done < num_samples) {
        size_t n = num_samples - done;
        if (n > WRITE_CHUNK) n = WRITE_CHUNK;

        for (size_t i = 0; i < n; i++) {
            int16_t v = clamp16((int32_t)in[done + i] * SPEAKER_GAIN);
            stereo[2 * i] = v;
            stereo[2 * i + 1] = v;
        }
        size_t written = 0;
        i2s_channel_write(s_tx_chan, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        done += n;
    }
    return num_samples;
}

void audio_hal_beep(int freq_hz, int duration_ms)
{
    int16_t tone[FRAME_SAMPLES];
    int total = SAMPLE_RATE_HZ * duration_ms / 1000;
    int half_period = SAMPLE_RATE_HZ / (2 * freq_hz);   // samples per half cycle of the square wave
    if (half_period < 1) half_period = 1;

    for (int pos = 0; pos < total; pos += FRAME_SAMPLES) {
        int n = (total - pos < FRAME_SAMPLES) ? (total - pos) : FRAME_SAMPLES;
        for (int i = 0; i < n; i++) {
            tone[i] = (((pos + i) / half_period) % 2) ? 8000 : -8000;
        }
        audio_hal_write(tone, (size_t)n);
    }
}
