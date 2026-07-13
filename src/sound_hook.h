/* sound_hook.h - decoupled sound trigger for the pure sim
 *
 * The simulation (people.c, game.c) must stay free of SDL/audio so the test
 * harness links without an audio backend. Instead the sim calls play_snd(id)
 * with a WAV resource id from audio_events.h; if main.c has installed a hook,
 * it forwards to audio_play(). Default hook is NULL — a no-op — so tests and
 * any headless sim run are unaffected.
 */
#ifndef SOUND_HOOK_H
#define SOUND_HOOK_H

#include "audio_events.h"

typedef void (*SoundHookFn)(int wav_id);
extern SoundHookFn g_sound_hook;

static inline void play_snd(int wav_id)
{
    if (g_sound_hook) g_sound_hook(wav_id);
}

#endif /* SOUND_HOOK_H */
