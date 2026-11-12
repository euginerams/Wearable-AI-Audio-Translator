#include "language.h"
#include "config.h"

typedef struct {
    const char *code;
    const char *name;
} language_t;

// Same order as the beeps: 1 beep = English, 2 = Spanish, 3 = French, 4 = German, 5 = Portuguese,
// 6 = Afrikaans. The codes must match languages.py on the server.
static const language_t LANGUAGES[] = {
    { "en", "English" },
    { "es", "Spanish" },
    { "fr", "French" },
    { "de", "German" },
    { "pt", "Portuguese" },
    { "af", "Afrikaans" },
};
#define LANGUAGE_COUNT ((int)(sizeof(LANGUAGES) / sizeof(LANGUAGES[0])))

static volatile int s_index = DEFAULT_TARGET_INDEX;
static volatile bool s_pending = true;     // the server must be told at start-up

int language_count(void) { return LANGUAGE_COUNT; }
int language_index(void) { return s_index; }
const char *language_code(int index) { return LANGUAGES[index % LANGUAGE_COUNT].code; }
const char *language_name(int index) { return LANGUAGES[index % LANGUAGE_COUNT].name; }

int language_next(void)
{
    s_index = (s_index + 1) % LANGUAGE_COUNT;
    s_pending = true;
    return s_index;
}

bool language_pending(void) { return s_pending; }
void language_clear_pending(void) { s_pending = false; }
void language_set_pending(void) { s_pending = true; }
