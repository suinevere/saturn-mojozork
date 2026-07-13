/*----------------------
 | sound.h
 | Description: The PCM sound-effect engine's interface behind the Z-machine
 |   sound_effect opcode: load a game's .BLB, play/service/stop effects, and query
 |   availability and level. Implemented in sound.cxx.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef SATURN_SOUND_H
#define SATURN_SOUND_H
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | sound_init
 | Description: Loads the sound index for the loaded game from CD file `blbfile`
 |   (e.g. "LRKHOROR.BLB"). An absent/unparsable file leaves sound off (no error).
 | Author: suinevere
 ----------------------*/
void sound_init(const char* blbfile);

/*----------------------
 | saturn_sound_effect
 | Description: The Z-machine sound_effect hook: effect 1=prepare, 2=start,
 |   3=stop, 4=finish; volume 1-8 (255=default). A no-op when disabled or the
 |   sound is unknown.
 | Author: suinevere
 ----------------------*/
void saturn_sound_effect(int number, int effect, int volume);

/*----------------------
 | sound_service / sound_stop_all
 | Description: service (once per input frame) re-triggers looping sounds and reaps
 |   finished ones; stop_all stops every channel and frees buffers (game switch /
 |   reboot).
 | Author: suinevere
 ----------------------*/
void sound_service(void);
void sound_stop_all(void);

/*----------------------
 | sound_has_audio / sound_set_enabled / sound_set_level
 | Description: has_audio is 1 when a .BLB loaded (PCM effects available);
 |   set_enabled is the Options toggle (disabling stops everything); set_level sets
 |   the PCM output level 0..7 (0 = silence, scales effect volume and disables).
 | Author: suinevere
 ----------------------*/
int sound_has_audio(void);
void sound_set_enabled(int on);
void sound_set_level(int level);

#ifdef __cplusplus
}
#endif
#endif
