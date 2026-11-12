#include "button.h"
#include "config.h"
#include "audio_hal.h"
#include "language.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";

static volatile bool s_session_active = false;

// Returns true once for each press of the button on `pin` (debounced).
// `released` remembers whether the button has been let go since the last press.
static bool pressed(int pin, bool *released)
{
    bool down = (gpio_get_level(pin) == 0);      // pressed = pin pulled to GND
    if (down && *released) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        if (gpio_get_level(pin) == 0) {
            *released = false;
            return true;
        }
    }
    if (!down) {
        *released = true;
    }
    return false;
}

// One beep per position in the language list: 1 = English ... 5 = Portuguese, 6 = Afrikaans
static void announce_language(void)
{
    int index = language_index();
    ESP_LOGI(TAG, "target language: %s (%s)", language_code(index), language_name(index));
    for (int i = 0; i <= index; i++) {
        audio_hal_beep(1500, 80);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

static void button_task(void *arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_BUTTON) | (1ULL << PIN_BUTTON_LANG),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    bool session_released = true;
    bool language_released = true;

    while (1) {
        if (pressed(PIN_BUTTON, &session_released)) {
            s_session_active = !s_session_active;
            ESP_LOGI(TAG, "session %s", s_session_active ? "STARTED" : "STOPPED");
            audio_hal_beep(s_session_active ? 1200 : 600, 150);
        }
        if (pressed(PIN_BUTTON_LANG, &language_released)) {
            language_next();
            announce_language();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void button_init(void)
{
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
}

bool button_session_active(void)
{
    return s_session_active;
}
