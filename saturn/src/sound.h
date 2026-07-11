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

/* Options toggle. Disabling stops everything. */
void sound_set_enabled(int on);

#ifdef __cplusplus
}
#endif
#endif
