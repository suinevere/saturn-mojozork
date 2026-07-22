/*----------------------
 | netbin_sound.cxx
 | Description: RAM-based SGL sound driver bring-up for the .netbin build.
 |   Mirrors SRL's Sound::Hardware::Initialize() (srl_sound.hpp:24) call for
 |   call -- slSoundOffWait, SND_Init, SND_ChgMap, slInitSound, the SCSP
 |   enable poke, CDC_CdInit, SND_SetCdDaLev, slSoundOnWait -- but sources the
 |   driver program and area map from netbin_blobs instead of Cd::File. SRL's
 |   version is left to run and harmlessly skip itself: its body is guarded on
 |   both files existing on the disc, which they do not under the PlanetWeb
 |   loader.
 | Author: suinevere
 | Dependencies: netbin_blobs.h, netbin_sound.h, SGL
 ----------------------*/

#include "netbin_sound.h"

#ifdef NETBIN

#include <srl.hpp>

extern "C" {
#include "netbin_blobs.h"
}

extern "C" void netbin_sound_init(void) {
    const unsigned char *prg = netbin_sddrvs_data();
    const unsigned char *ara = netbin_bootsnd_data();
    unsigned int prg_len = netbin_sddrvs_size();
    unsigned int ara_len = netbin_bootsnd_size();

    if (prg == 0 || ara == 0 || prg_len == 0 || ara_len == 0) return;

    slSoundOffWait();

    // SND_Init/slInitSound take non-const pointers but only read the buffers.
    // The arrays live in .rodata; casting away const is safe here and avoids
    // spending ~26 KB of RAM on a copy we would never write to.
    uint8_t *prgBuf = (uint8_t *) prg;
    uint8_t *araBuf = (uint8_t *) ara;

    SndIniDt init;
    SND_INI_PRG_ADR(init) = (uint16_t *) prgBuf;
    SND_INI_PRG_SZ(init)  = (uint16_t) prg_len;
    SND_INI_ARA_ADR(init) = (uint16_t *) araBuf;
    SND_INI_ARA_SZ(init)  = (uint16_t) ara_len;
    SND_Init(&init);
    SND_ChgMap(0);

    slInitSound(prgBuf, prg_len, araBuf, ara_len);
    *(volatile unsigned char *) (0x25a004e1) = 0x0;
    CDC_CdInit(0x00, 0x00, 0x05, 0x0f);
    SND_SetCdDaLev(7, 7);

    // Sound back on. SRL's Initialize() calls slSoundOffWait/slSoundOnWait
    // outside its file-exists guard, so it has already left sound ON by the
    // time we run; without this the slSoundOffWait above would stick and every
    // sound in the netbin build would be silent.
    slSoundOnWait();
}

#else  /* !NETBIN -- SRL's own Cd::File path handles this in the CD build */

extern "C" void netbin_sound_init(void) { }

#endif
