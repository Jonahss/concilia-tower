/* sound_hook.c - definition of the sim's sound hook (see sound_hook.h).
 * Kept in its own translation unit with NO SDL/audio dependency so both the
 * game and the test harness can link it. */
#include "sound_hook.h"

SoundHookFn g_sound_hook = 0;
