/* ame-next — audio: deterministic synth + decoded samples (audio.txt).
 *
 * ONE C implementation; web gets the same C via Emscripten. The audio
 * callback only pulls samples (audio_render); it never calls game logic and
 * never allocates. The sim publishes control through a lock-free command
 * queue (single producer = logic thread, single consumer = audio thread).
 *
 * Determinism: voice phases are 32.32 fixed-point integer accumulators, so
 * a fixed sample rate + command schedule renders byte-identical buffers per
 * binary (golden tests). Envelope/pan math is float but ordered identically
 * per binary (FP flags pinned in the build).
 */
#ifndef AME_AUDIO_H
#define AME_AUDIO_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_AUDIO_VOICES 32
#define AME_AUDIO_CMD_CAP 128

typedef enum {
    AME_WAVE_SINE = 0,
    AME_WAVE_SQUARE,
    AME_WAVE_SAW,
    AME_WAVE_TRIANGLE,
    AME_WAVE_NOISE,
    AME_WAVE_PCM /* decoded-sample voice: reads a PCM buffer (setup
                   layer: the buffer is loaded ONCE and owned by the
                   caller; the mixer only reads it - audio.txt) */
} ame_wave;

/* synth patch — fixed struct, no allocation (two-layer rule: setup data) */
typedef struct {
    ame_wave wave;
    float    freq;      /* Hz */
    float    gain;      /* 0..1 */
    float    pan;       /* -1..1 (constant-power) */
    float    attack;    /* seconds to full */
    float    hold;      /* seconds at full (then release); loop ignores */
    float    release;   /* seconds to zero */
    bool     loop;      /* hold attack level indefinitely */
} ame_synth_cfg;

enum {
    AME_AUCMD_PLAY = 0, /* -> id */
    AME_AUCMD_STOP,     /* -> id */
    AME_AUCMD_SET_FREQ, /* -> id, f1 */
    AME_AUCMD_SET_GAIN, /* -> id, f1 */
    AME_AUCMD_SET_PAN,  /* -> id, f1 */
    AME_AUCMD_QUIT_VOICE,
};

typedef struct {
    uint8_t cmd;      /* AME_AUCMD_* */
    uint8_t id;
    float   f1;
} ame_aucmd;

/* --- lifecycle ------------------------------------------------------------ */
/* Init the mixer for headless/testing. rate in Hz (48000 typical), channels
 * 1 or 2. Deterministic for a fixed rate+schedule. */
void audio_init(int sample_rate, int channels);
void audio_shutdown(void);

/* Attach to a real SDL audio device (engine app calls this on desktop).
 * Safe no-op when audio can't open (CI) — mixing state still usable. */
void audio_attach_sdl(void);
void audio_detach_sdl(void);

/* --- voices (logic thread; published via command queue) -------------------- */
/* create a voice slot (setup: cfg is copied); returns id or -1 when full */
int  audio_new_synth(const ame_synth_cfg *cfg);
/* decoded sample voice: stereo interleaved float PCM, loaded once by
 * the caller (see audio_load_wav). Returns a voice id, -1 if full. */
int  audio_new_decoded(const float *pcm_stereo, int frames, bool loop);
/* read a 16-bit PCM wav (mono or stereo) into NEWLY-ALLOCATED stereo
 * interleaved float samples (setup layer: call once, free() when the
 * voice is gone). Returns NULL on any format mismatch. */
float *audio_load_wav(const char *path, int *frames_out);
void audio_set(int id, const ame_synth_cfg *cfg); /* republish patch */
void audio_play(int id);
void audio_stop(int id);
void audio_master(float gain);        /* 0..1 */
bool audio_voice_active(int id);      /* published by the audio thread */

/* beat/energy helper for gameplay visuals: 0..1 envelope of a voice,
 * published by the render/mix side (one writer: audio thread) */
float audio_beat_amplitude(int id);

/* --- mixing (audio thread OR tests; the ONLY place samples are written) --- */
/* render interleaved samples into out (frames * channels floats). Pulls
 * pending commands first. No allocation, no logic calls, no file I/O. */
void audio_render(float *out, int frames);

/* diagnostics */
uint32_t audio_dropped_cmds(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_AUDIO_H */
