/* audio.h - SimTower sound playback
 *
 * SimTower ships 58 WAV effects inside SIMTOWER.EXE (NE resource type
 * 0xFF0A, ~11kHz 8-bit mono). This module decodes them from the already-open
 * NE table, converts to the device format once, and mixes fire-and-forget
 * voices on the SDL audio callback.
 *
 * FAITHFULNESS NOTE: this module is mapping-agnostic on purpose. It plays a
 * clip by its NE resource id; the event -> sound-id table (which effect fires
 * on which game event, plus throttles) lives in audio_events.c and is filled
 * strictly from the decomp referee, never guessed here.
 *
 * HEADLESS NOTE: the Pi has no soundcard, so audio_init() falls back to SDL's
 * dummy driver. The mixer still runs, which lets audio_capture_* tee the mixed
 * output to a WAV for verification / sending a clip to Discord.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include "ne_resource.h"

/* Device output format (what the mixer produces). */
#define AUDIO_DEV_FREQ 22050

/* Open the audio device (or dummy fallback). Returns 0 if a real or dummy
 * device is running, -1 if audio is fully disabled. Safe to call once. */
int  audio_init(void);

/* Capture/offline mode: no device is opened. audio_play() enqueues voices and
 * audio_advance() mixes them deterministically into the capture buffer. Used
 * to render "tower in motion" clips headless. Returns 0. */
int  audio_init_capture(void);

void audio_shutdown(void);

/* Decode every 0xFF0A WAV in the NE table into device-format clips.
 * Returns the number of clips successfully loaded. */
int  audio_load_from_ne(NEResourceTable *exe);
int  audio_clip_count(void);
int  audio_has_clip(uint16_t ne_id);

/* Fire-and-forget playback of the clip with this NE resource id. gain is a
 * linear multiplier (1.0 = source level). No-op if the id is unknown, audio
 * is disabled, or all voices are busy. */
void audio_play(uint16_t ne_id, float gain);

void audio_set_enabled(int on);
int  audio_is_enabled(void);

/* Looping playback for held actions (the build-drag clatter, referee row 18).
 * start_loop is idempotent for the same clip; only one loop plays at a time. */
void audio_start_loop(uint16_t ne_id, float gain);
void audio_stop_loop(void);
void audio_stop_all(void);   /* silence every voice (world reset) */

/* Offline/headless capture: begin accumulating the mixed output, then write it
 * to a mono 16-bit WAV. Used to verify playback and to send clips to Discord.
 * Between begin and write, drive the game (or audio_advance) so voices fire. */
void audio_capture_begin(void);
int  audio_capture_write_wav(const char *path);  /* stops + writes; 0 on success */

/* Deterministic offline advance: mix `frames` of silence+voices without a
 * device, appending to the capture buffer. Lets a test render a clip without
 * waiting on the realtime callback. */
void audio_advance(int frames);

/* The pure mixer. SDL's callback and audio_advance both call this. Adds active
 * voices into `out` (mono S16, `frames` samples). */
void audio_mix_s16(int16_t *out, int frames);

#endif /* AUDIO_H */
