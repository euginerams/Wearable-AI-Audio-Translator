#pragma once
#include <stdbool.h>

// Starts the task that watches the two buttons:
//   PIN_BUTTON       starts / stops a listening session
//   PIN_BUTTON_LANG  chooses the next target language
// Call once from app_main after audio_hal_init().
void button_init(void);

// True while a listening session is active.
bool button_session_active(void);
