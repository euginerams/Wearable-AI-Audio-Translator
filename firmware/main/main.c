#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "audio_hal.h"
#include "dsp.h"
#include "vad.h"
#include "audio_buffer.h"
#include "net_stream.h"
#include "button.h"
#include "language.h"
#include "metrics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "translator";

#define UL(x) ((unsigned long)(x))

// Ring buffers absorb jitter between the fixed-rate audio I/O and the
// bursty Wi-Fi send/receive calls.
#define SEND_BUF_SAMPLES        4096   // power of two (about 256 ms of audio)
#define PLAYBACK_BUF_SAMPLES    4096
#define MUTE_AFTER_PLAYBACK_MS  600    // ignore the microphone this long after playing audio
#define STATS_EVERY_FRAMES      50     // 50 frames x 20 ms = one status line per second
#define END_OF_SPEECH_FRAMES    (END_OF_SPEECH_MS / FRAME_MS)

static audio_ring_buf_t *s_send_buf;
static audio_ring_buf_t *s_playback_buf;

// While the speaker is playing, ignore the microphone so the device
// does not translate its own voice.
static volatile uint32_t s_mute_until_ms = 0;

// Used to measure the delay between the end of speech and the translated audio.
static volatile uint32_t s_last_sent_ms = 0;
static volatile bool s_awaiting_reply = false;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Tells the server which language to translate into. Only the capture task sends,
// so frames from different tasks can never be mixed on the connection.
static void send_language_if_needed(void)
{
    if (!language_pending() || !net_stream_is_connected()) return;

    const char *code = language_code(language_index());
    if (net_stream_send_control(NET_FRAME_SET_LANGUAGE, (const uint8_t *)code, strlen(code))) {
        language_clear_pending();
        ESP_LOGI(TAG, "target language sent to the server: %s", code);
    }
}

// ---------------------------------------------------------------------
// Capture task: microphone -> DSP -> voice detection -> send buffer -> Wi-Fi
// ---------------------------------------------------------------------
static void capture_task(void *arg)
{
    int16_t frame[FRAME_SAMPLES];
    int frame_count = 0;
    int n_speech = 0, n_silence = 0;
    double sum_raw = 0, sum_filtered = 0, sum_dc = 0, sum_speech = 0, sum_silence = 0;
    uint32_t prev_bytes_sent = 0;
    bool utterance_open = false;     // speech has been sent and the sentence is not finished yet
    int silent_frames = 0;           // frames in a row without speech

    while (1) {
        vTaskDelay(1);   // give the idle task (and the watchdog) a chance to run

        size_t n = audio_hal_read(frame, FRAME_SAMPLES);
        if (n != FRAME_SAMPLES) continue;

        // Measure the signal before filtering: DC offset and loudness
        int32_t sum = 0;
        for (size_t i = 0; i < n; i++) sum += frame[i];
        float dc = (float)sum / (float)n;
        float rms_raw = dsp_rms(frame, n);

        // Remove the DC offset, then decide whether this frame contains speech
        dsp_high_pass(frame, n);
        float rms = dsp_rms(frame, n);
        bool speaking = vad_process(rms);

        sum_raw += rms_raw;
        sum_filtered += rms;
        sum_dc += dc;
        if (speaking) {
            sum_speech += rms;
            n_speech++;
            silent_frames = 0;
        } else {
            sum_silence += rms;
            n_silence++;
            silent_frames++;
        }

        // One status line per second, written so it can be copied into a table
        if (++frame_count == STATS_EVERY_FRAMES) {
            char snr[16] = "n/a";
            if (n_speech > 0 && n_silence > 0 && sum_silence > 0) {
                float speech_avg = (float)(sum_speech / n_speech);
                float silence_avg = (float)(sum_silence / n_silence);
                if (silence_avg > 0.5f && speech_avg > 0) {
                    snprintf(snr, sizeof(snr), "%.1f", 20.0f * log10f(speech_avg / silence_avg));
                }
            }
            uint32_t tx_bytes = g_metrics.bytes_sent - prev_bytes_sent;
            prev_bytes_sent = g_metrics.bytes_sent;

            ESP_LOGI("stats",
                     "raw_rms=%.0f dc=%.0f rms=%.0f floor=%.0f snr_db=%s speech=%d/%d "
                     "session=%d lang=%s link=%d tx_bytes_s=%lu frames_sent=%lu utterances=%lu "
                     "drops(send/play/nolink)=%lu/%lu/%lu send_fail=%lu reconnects=%lu recovery_ms=%lu "
                     "rx_frames=%lu latency_ms=%lu stack_free_B=%lu",
                     sum_raw / frame_count, sum_dc / frame_count, sum_filtered / frame_count,
                     vad_noise_floor(), snr, n_speech, frame_count,
                     button_session_active(), language_code(language_index()), net_stream_is_connected(),
                     UL(tx_bytes), UL(g_metrics.frames_sent), UL(g_metrics.utterances_sent),
                     UL(g_metrics.send_buf_dropped_samples),
                     UL(g_metrics.playback_buf_dropped_samples),
                     UL(g_metrics.frames_dropped_no_link),
                     UL(g_metrics.send_failures), UL(g_metrics.reconnects),
                     UL(g_metrics.last_recovery_ms), UL(g_metrics.frames_received),
                     UL(g_metrics.last_latency_ms), UL(uxTaskGetStackHighWaterMark(NULL)));

            frame_count = 0;
            n_speech = n_silence = 0;
            sum_raw = sum_filtered = sum_dc = sum_speech = sum_silence = 0;
        }

        send_language_if_needed();

        // Only send speech, only during a session, and never our own playback
        bool muted = (int32_t)(s_mute_until_ms - now_ms()) > 0;
        if (button_session_active() && speaking && !muted) {
            size_t pushed = audio_buffer_push(s_send_buf, frame, n);
            if (pushed < n) {
                g_metrics.send_buf_dropped_samples += (uint32_t)(n - pushed);
            }
        }

        if (audio_buffer_available(s_send_buf) >= FRAME_SAMPLES) {
            int16_t out_frame[FRAME_SAMPLES];
            size_t popped = audio_buffer_pop(s_send_buf, out_frame, FRAME_SAMPLES);
            if (net_stream_is_connected()) {
                if (net_stream_send_frame(out_frame, popped)) {
                    g_metrics.frames_sent++;
                    g_metrics.bytes_sent += (uint32_t)(popped * sizeof(int16_t));
                    s_last_sent_ms = now_ms();
                    s_awaiting_reply = true;
                    utterance_open = true;
                } else {
                    g_metrics.send_failures++;
                    ESP_LOGW(TAG, "send failed; the network task will reconnect");
                }
            } else {
                g_metrics.frames_dropped_no_link++;   // link down: this speech is lost
            }
        }

        // Silence for long enough after speech: the sentence is finished, so tell
        // the server to translate it now. Capturing stops by itself; the session stays on.
        if (utterance_open && silent_frames >= END_OF_SPEECH_FRAMES &&
            audio_buffer_available(s_send_buf) < FRAME_SAMPLES) {
            if (net_stream_is_connected() &&
                net_stream_send_control(NET_FRAME_END_OF_UTTERANCE, NULL, 0)) {
                g_metrics.utterances_sent++;
                ESP_LOGI(TAG, "end of speech: sentence %lu sent for translation",
                         UL(g_metrics.utterances_sent));
            }
            utterance_open = false;
        }
    }
}

// ---------------------------------------------------------------------
// Playback task: Wi-Fi -> playback buffer -> speaker
// ---------------------------------------------------------------------
static void playback_task(void *arg)
{
    int16_t frame[FRAME_SAMPLES];

    while (1) {
        size_t n = net_stream_recv_frame(frame, FRAME_SAMPLES);
        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        g_metrics.frames_received++;

        if (s_awaiting_reply) {
            s_awaiting_reply = false;
            g_metrics.last_latency_ms = now_ms() - s_last_sent_ms;
            ESP_LOGI("latency", "translated audio started %lu ms after the last speech frame was sent",
                     UL(g_metrics.last_latency_ms));
        }

        size_t pushed = audio_buffer_push(s_playback_buf, frame, n);
        if (pushed < n) {
            g_metrics.playback_buf_dropped_samples += (uint32_t)(n - pushed);
        }

        if (audio_buffer_available(s_playback_buf) >= FRAME_SAMPLES) {
            int16_t out_frame[FRAME_SAMPLES];
            size_t popped = audio_buffer_pop(s_playback_buf, out_frame, FRAME_SAMPLES);
            audio_hal_write(out_frame, popped);
            s_mute_until_ms = now_ms() + MUTE_AFTER_PLAYBACK_MS;
        }
    }
}

// ---------------------------------------------------------------------
// Network task: keeps the connection to the translation server alive.
// It detects a lost link, reconnects, and records how long recovery took.
// ---------------------------------------------------------------------
static void net_task(void *arg)
{
    bool ever_connected = false;
    bool was_connected = false;
    uint32_t down_since_ms = now_ms();

    while (1) {
        if (net_stream_is_connected()) {
            was_connected = true;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (was_connected) {
            was_connected = false;
            down_since_ms = now_ms();
            ESP_LOGW(TAG, "link to the translation server lost");
        }

        if (net_stream_connect()) {
            uint32_t down_ms = now_ms() - down_since_ms;
            if (ever_connected) {
                g_metrics.reconnects++;
                g_metrics.last_recovery_ms = down_ms;
                ESP_LOGI(TAG, "link restored after %lu ms", UL(down_ms));
            } else {
                ESP_LOGI(TAG, "connected to the translation server (%lu ms after start)", UL(down_ms));
            }
            language_set_pending();     // a new connection must be told the language again
            ever_connected = true;
            was_connected = true;
        } else {
            ESP_LOGW(TAG, "cannot reach the translation server at %s:%d, retrying", SERVER_IP, SERVER_PORT);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "wearable translator starting");

    audio_hal_init();

    // Speaker self-test: if this beep is heard, the amplifier and speaker work
    ESP_LOGI(TAG, "speaker self-test: playing a short beep");
    audio_hal_beep(1000, 400);

    dsp_init();
    vad_init();
    button_init();

    s_send_buf = audio_buffer_create(SEND_BUF_SAMPLES);
    s_playback_buf = audio_buffer_create(PLAYBACK_BUF_SAMPLES);

    // The audio tasks start first, so the microphone can be checked
    // even when the network or the server is not available.
    xTaskCreate(capture_task, "capture_task", 6144, NULL, 5, NULL);
    xTaskCreate(playback_task, "playback_task", 6144, NULL, 5, NULL);

    net_stream_wifi_init();    // waits until Wi-Fi is connected
    xTaskCreate(net_task, "net_task", 4096, NULL, 4, NULL);
}
