#ifndef AME_AUDIO_H
#define AME_AUDIO_H

/*
 * Mixer + tiny synth. No file I/O. The audio callback only calls
 * ame_audio_mix; the sim/main thread only calls play/cue.
 *
 * SETUP: ame_audio_reset(rate, channels)
 * HOT:   ame_audio_play_tone / ame_audio_mix
 */

enum { AME_AUDIO_VOICES = 8 };

void ame_audio_reset(int sample_rate, int channels);
void ame_audio_shutdown(void);

int  ame_audio_rate(void);
int  ame_audio_channels(void);

/* pan -1 left .. +1 right. decay_s is envelope length. */
void ame_audio_play_tone(float freq_hz, float gain, float decay_s, float pan);

void ame_audio_cue_click(void);
void ame_audio_cue_match(void);
void ame_audio_cue_miss(void);
void ame_audio_cue_win(void);
void ame_audio_cue_pickup(void);
void ame_audio_cue_boom(void);
void ame_audio_cue_jump(void);
void ame_audio_cue_hurt(void);
void ame_audio_cue_switch(void);

/* Mix `frames` of interleaved float PCM into `out` (channels from reset). */
void ame_audio_mix(float *out, int frames);

#endif
