#pragma once
#include <stdbool.h>

// The target language the user has chosen with the language button.
// The index is sent to the server as a language code (for example "fr").

int language_count(void);
int language_index(void);
const char *language_code(int index);
const char *language_name(int index);

// Moves to the next language (wraps around) and marks it as not yet sent.
int language_next(void);

// True while the current language still has to be sent to the server.
bool language_pending(void);
void language_clear_pending(void);
void language_set_pending(void);    // call after every (re)connection
