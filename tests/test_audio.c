/* test_audio.c - prove the sound pipeline end to end:
 *   SIMTOWER.EXE -> NE 0xFF0A decode -> device-format convert -> mix -> WAV.
 *
 * Headless-safe (falls back to SDL dummy driver). Renders a short demo clip
 * of a few sounds so we can listen / send it to Discord.
 *
 *   gcc -I src -o /tmp/ta tests/test_audio.c src/audio.c src/ne_resource.c \
 *       $(pkg-config --cflags --libs sdl2) -lm
 *   /tmp/ta <SIMTOWER.EXE> <out.wav> [id1 id2 ...]
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "ne_resource.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s EXE OUT.wav [ne_ids...]\n", argv[0]); return 2; }
    const char *exe = argv[1], *out = argv[2];

    if (audio_init() != 0) { fprintf(stderr, "audio_init failed\n"); return 1; }
    NEResourceTable t;
    if (ne_load(&t, exe) != 0) { fprintf(stderr, "ne_load failed\n"); return 1; }
    int n = audio_load_from_ne(&t);
    printf("loaded %d clips from %s\n", n, exe);
    if (n == 0) return 1;

    /* Which ids to demo: from argv, else the first few loaded. */
    uint16_t ids[16]; int nid = 0;
    for (int i = 3; i < argc && nid < 16; i++) ids[nid++] = (uint16_t)strtol(argv[i], NULL, 0);

    audio_capture_begin();
    if (nid == 0) {
        /* no ids given: play the first 6 loaded clips, spaced 0.6s apart */
        NEResourceList *l = ne_find_type(&t, NE_RT_SOUND);
        int lim = l->count < 6 ? l->count : 6;
        for (int i = 0; i < lim; i++) {
            printf("  play 0x%04x\n", l->items[i].id);
            audio_play(l->items[i].id, 1.0f);
            audio_advance(AUDIO_DEV_FREQ * 6 / 10);
        }
    } else {
        for (int i = 0; i < nid; i++) {
            printf("  play 0x%04x  (%s)\n", ids[i], audio_has_clip(ids[i]) ? "found" : "MISSING");
            audio_play(ids[i], 1.0f);
            audio_advance(AUDIO_DEV_FREQ * 8 / 10);
        }
    }
    audio_advance(AUDIO_DEV_FREQ / 2);  /* tail */

    if (audio_capture_write_wav(out) != 0) { fprintf(stderr, "write failed\n"); return 1; }
    printf("wrote %s\n", out);
    audio_shutdown();
    ne_free(&t);
    SDL_Quit();
    return 0;
}
