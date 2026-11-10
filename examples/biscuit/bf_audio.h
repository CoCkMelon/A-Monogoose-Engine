#ifndef BF_AUDIO_H
#define BF_AUDIO_H

/* Biscuit sfx on the trunk mixer (audio.txt): one synth voice per cue,
 * created once at init, retriggered by audio_play.
 *
 * Gameplay never calls audio directly: it pushes a game cue id (BF_EV_*)
 * into a tiny bounded ring on the logic thread; bf_cues_drain() (also
 * logic thread, once per fixed step) turns queued cues into audio_play.
 * That replaces the lineage's ame_events_subscribe sfx wiring with the
 * trunk audio API and keeps the sim free of engine audio calls. */

void bf_audio_init(void);
void bf_audio_shutdown(void);

/* logic thread */
void bf_cue_push(int kind);
void bf_cues_clear(void);
void bf_cues_drain(void);

#endif /* BF_AUDIO_H */
