/*----------------------
 | netbin_sound.h
 | Description: Initializes the SGL sound driver from the embedded SDDRVS.TSK
 |   and BOOTSND.MAP arrays instead of reading them off the CD. SRL's own
 |   Sound::Hardware::Initialize() (srl_core.hpp:107) runs first from inside
 |   SRL::Core::Initialize and finds no files under the PlanetWeb loader, so
 |   its body is skipped entirely -- this reruns the same sequence against RAM.
 |   Compiled to a no-op in the CD build, where SRL's own path succeeds.
 | Author: suinevere
 | Dependencies: netbin_blobs.h, SGL
 ----------------------*/

#ifndef NETBIN_SOUND_H
#define NETBIN_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Load the embedded SGL sound driver into the SCSP. Must be called after
   SRL::Core::Initialize() and before any Sound::Cdda or Sound::Pcm use.
   No-op when NETBIN is not defined. */
void netbin_sound_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NETBIN_SOUND_H */
