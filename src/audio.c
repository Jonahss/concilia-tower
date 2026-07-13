/* audio.c - SimTower sound playback (see audio.h) */
#include "audio.h"
#include "audio_events.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLIPS   128   /* the EXE has 58 sounds; headroom */
#define MAX_VOICES  16    /* simultaneous effects */

typedef struct {
    uint16_t ne_id;
    int16_t *pcm;         /* device-format mono S16 */
    int      frames;      /* sample count */
} Clip;

typedef struct {
    const Clip *clip;
    int   pos;            /* next sample index */
    float gain;
    int   active;
} Voice;

static struct {
    int            ready;        /* device (or dummy) open */
    int            enabled;      /* user toggle (SoundT 0x252 analogue) */
    SDL_AudioDeviceID dev;
    Clip           clips[MAX_CLIPS];
    int            clip_count;
    Voice          voices[MAX_VOICES];

    /* capture tap */
    int            capturing;
    int16_t       *cap_buf;
    int            cap_len;
    int            cap_cap;
} A;

/* ---- clip lookup ---- */
static const Clip *find_clip(uint16_t ne_id)
{
    for (int i = 0; i < A.clip_count; i++)
        if (A.clips[i].ne_id == ne_id) return &A.clips[i];
    return NULL;
}

int audio_has_clip(uint16_t ne_id) { return find_clip(ne_id) != NULL; }
int audio_clip_count(void) { return A.clip_count; }
int audio_is_enabled(void) { return A.enabled; }
void audio_set_enabled(int on) { A.enabled = on ? 1 : 0; }

/* ---- the mixer (audio-thread + offline both) ---- */
void audio_mix_s16(int16_t *out, int frames)
{
    memset(out, 0, (size_t)frames * sizeof(int16_t));
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vo = &A.voices[v];
        if (!vo->active || !vo->clip) continue;
        const Clip *c = vo->clip;
        int n = frames;
        if (vo->pos + n > c->frames) n = c->frames - vo->pos;
        for (int i = 0; i < n; i++) {
            int s = out[i] + (int)(c->pcm[vo->pos + i] * vo->gain);
            if (s >  32767) s =  32767;
            if (s < -32768) s = -32768;
            out[i] = (int16_t)s;
        }
        vo->pos += n;
        if (vo->pos >= c->frames) { vo->active = 0; vo->clip = NULL; }
    }
}

/* ---- capture tap ---- */
static void capture_append(const int16_t *buf, int frames)
{
    if (!A.capturing) return;
    if (A.cap_len + frames > A.cap_cap) {
        int nc = A.cap_cap ? A.cap_cap * 2 : AUDIO_DEV_FREQ * 4;
        while (nc < A.cap_len + frames) nc *= 2;
        int16_t *nb = realloc(A.cap_buf, (size_t)nc * sizeof(int16_t));
        if (!nb) return;  /* drop silently rather than crash */
        A.cap_buf = nb; A.cap_cap = nc;
    }
    memcpy(A.cap_buf + A.cap_len, buf, (size_t)frames * sizeof(int16_t));
    A.cap_len += frames;
}

static void SDLCALL audio_callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int frames = len / (int)sizeof(int16_t);
    int16_t *out = (int16_t *)stream;
    audio_mix_s16(out, frames);
    capture_append(out, frames);
}

/* ---- init / shutdown ---- */
int audio_init(void)
{
    if (A.ready) return 0;
    A.enabled = 1;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_DEV_FREQ;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;

    /* Ensure the audio subsystem is up (main may have SDL_INIT_AUDIO'd already). */
    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);

    A.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (A.dev == 0) {
        /* Headless Pi: no sink. Fall back to the dummy driver so the mixer and
         * capture tap still run. */
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        SDL_InitSubSystem(SDL_INIT_AUDIO);
        A.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (A.dev == 0) {
            fprintf(stderr, "audio: no device (%s); sound disabled\n", SDL_GetError());
            return -1;
        }
        fprintf(stderr, "audio: using dummy driver (headless); capture available\n");
    }
    SDL_PauseAudioDevice(A.dev, 0);
    A.ready = 1;
    return 0;
}

int audio_init_capture(void)
{
    /* No device: the game loop drives audio_advance() itself. */
    A.enabled = 1;
    A.dev = 0;
    A.ready = 1;
    return 0;
}

void audio_shutdown(void)
{
    if (A.dev) { SDL_CloseAudioDevice(A.dev); A.dev = 0; }
    for (int i = 0; i < A.clip_count; i++) free(A.clips[i].pcm);
    A.clip_count = 0;
    free(A.cap_buf); A.cap_buf = NULL; A.cap_cap = A.cap_len = 0;
    A.capturing = 0; A.ready = 0;
}

/* ---- loading: RIFF resource -> device-format clip ---- */
static int decode_wav(const NEResource *r, Clip *out)
{
    if (!r->data || r->length < 12 || memcmp(r->data, "RIFF", 4) != 0)
        return -1;
    SDL_RWops *rw = SDL_RWFromConstMem(r->data, r->length);
    if (!rw) return -1;
    SDL_AudioSpec spec; Uint8 *buf = NULL; Uint32 blen = 0;
    if (!SDL_LoadWAV_RW(rw, 1, &spec, &buf, &blen)) return -1;

    /* Convert to device format (mono S16 @ AUDIO_DEV_FREQ). SDL_AudioStream
     * handles format + rate + channel conversion robustly. */
    SDL_AudioStream *st = SDL_NewAudioStream(spec.format, spec.channels, spec.freq,
                                             AUDIO_S16SYS, 1, AUDIO_DEV_FREQ);
    if (!st) { SDL_FreeWAV(buf); return -1; }
    SDL_AudioStreamPut(st, buf, (int)blen);
    SDL_AudioStreamFlush(st);
    int avail = SDL_AudioStreamAvailable(st);
    int16_t *pcm = NULL;
    if (avail > 0) {
        pcm = malloc((size_t)avail);
        if (pcm) SDL_AudioStreamGet(st, pcm, avail);
    }
    SDL_FreeAudioStream(st);
    SDL_FreeWAV(buf);
    if (!pcm || avail <= 0) { free(pcm); return -1; }

    out->ne_id  = r->id;
    out->pcm    = pcm;
    out->frames = avail / (int)sizeof(int16_t);
    return 0;
}

int audio_load_from_ne(NEResourceTable *exe)
{
    NEResourceList *l = ne_find_type(exe, NE_RT_SOUND);
    if (!l) return 0;
    for (int i = 0; i < l->count && A.clip_count < MAX_CLIPS; i++) {
        Clip c;
        if (decode_wav(&l->items[i], &c) == 0)
            A.clips[A.clip_count++] = c;
    }
    return A.clip_count;
}

/* Priority class for channel budgeting. The EXE buckets sounds into classes
 * with a per-class max concurrent-channel count ([0xDE2A/2C/2E]); the elevator
 * ding/depart are the lowest class, which is why a busy tower dings steadily
 * but never drowns everything. We reproduce the audible effect: low-class
 * sounds get a small concurrency budget so they can't hog all the voices.
 * referee_sound_events_2026-07-13.md, "Priority = channel budgeting". */
#define LOW_CLASS_MAX 2
static int is_low_class(uint16_t ne_id)
{
    return ne_id == SND_ELEV_DING || ne_id == SND_ELEV_DEPART;
}

/* ---- playback ---- */
void audio_play(uint16_t ne_id, float gain)
{
    if (!A.ready || !A.enabled) return;
    const Clip *c = find_clip(ne_id);
    if (!c) return;
    if (A.dev) SDL_LockAudioDevice(A.dev);
    int slot = -1, low_active = 0;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!A.voices[v].active) { if (slot < 0) slot = v; continue; }
        if (is_low_class(A.voices[v].clip ? A.voices[v].clip->ne_id : 0)) low_active++;
    }
    /* Drop a low-class sound if its budget is full (rather than let dings
     * pile up into a continuous wall). */
    if (is_low_class(ne_id) && low_active >= LOW_CLASS_MAX) slot = -1;
    if (slot >= 0) {
        A.voices[slot].clip = c;
        A.voices[slot].pos = 0;
        A.voices[slot].gain = gain;
        A.voices[slot].active = 1;
    }
    if (A.dev) SDL_UnlockAudioDevice(A.dev);
}

/* ---- capture ---- */
void audio_capture_begin(void)
{
    if (A.dev) SDL_LockAudioDevice(A.dev);
    A.cap_len = 0;
    A.capturing = 1;
    if (A.dev) SDL_UnlockAudioDevice(A.dev);
}

void audio_advance(int frames)
{
    /* Deterministic offline mixing in chunks, appended to the capture buffer. */
    int16_t tmp[1024];
    while (frames > 0) {
        int n = frames < 1024 ? frames : 1024;
        audio_mix_s16(tmp, n);
        capture_append(tmp, n);
        frames -= n;
    }
}

static void put_u32(FILE *f, uint32_t v) { fputc(v&0xff,f);fputc((v>>8)&0xff,f);fputc((v>>16)&0xff,f);fputc((v>>24)&0xff,f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v&0xff,f);fputc((v>>8)&0xff,f); }

int audio_capture_write_wav(const char *path)
{
    if (A.dev) SDL_LockAudioDevice(A.dev);
    A.capturing = 0;
    int n = A.cap_len;
    if (A.dev) SDL_UnlockAudioDevice(A.dev);
    if (n <= 0) return -1;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data_bytes = (uint32_t)n * 2;
    fwrite("RIFF", 1, 4, f); put_u32(f, 36 + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put_u32(f, 16); put_u16(f, 1);      /* PCM */
    put_u16(f, 1);                                                /* mono */
    put_u32(f, AUDIO_DEV_FREQ);
    put_u32(f, AUDIO_DEV_FREQ * 2);                               /* byte rate */
    put_u16(f, 2); put_u16(f, 16);                                /* block align, bits */
    fwrite("data", 1, 4, f); put_u32(f, data_bytes);
    fwrite(A.cap_buf, sizeof(int16_t), (size_t)n, f);
    fclose(f);
    return 0;
}
