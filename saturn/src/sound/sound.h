#ifndef SATURN_SOUND_H
#define SATURN_SOUND_H
#ifdef __cplusplus
extern "C" {
#endif

/* Load the sound index for the loaded game from CD file `blbfile` (e.g.
   "LRKHOROR.BLB"). Absent/unparsable file -> sound stays off (no error). */
void sound_init(const char* blbfile);

/* Z-machine sound_effect hook: effect 1=prepare, 2=start, 3=stop, 4=finish;
   volume 1-8 (255=default). No-op when disabled or the sound is unknown. */
void saturn_sound_effect(int number, int effect, int volume);

/* Call once per input frame: re-trigger looping sounds, reap finished ones. */
void sound_service(void);

/* Stop all channels + free buffers (game switch / reboot). */
void sound_stop_all(void);

/* 1 if a sound index (.BLB) loaded for the current game -> PCM effects available.
   0 when the game ships no .BLB (or it was absent/unparsable). */
int sound_has_audio(void);

/* Options toggle. Disabling stops everything. */
void sound_set_enabled(int on);
/* PCM output level 0..7 (0 = silence). Scales effect volume; 0 disables. */
void sound_set_level(int level);

#ifdef __cplusplus
}
#endif
#endif
