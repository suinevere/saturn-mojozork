/*----------------------
 | sound.cxx
 | Description: The Saturn PCM sound-effect engine behind the Z-machine's
 |   sound_effect opcode. Loads effect samples out of the game's .BLB (Blorb)
 |   file, plays them on SRL's four PCM channels, and keeps looping effects alive
 |   with a ping-pong hand-off so they never gap. Every sample is read from the CD
 |   once and cached for the game's lifetime, so re-triggers never re-read the
 |   disc (which would interrupt CD-DA music -- the drive has one head).
 | Author: suinevere
 | Dependencies: sound.h (the opcode/host interface), sound_blorb.h (the Blorb
 |   index + sample lookup), SRL (Sound::Pcm, Cd::File, Memory)
 ----------------------*/
#include <srl.hpp>
extern "C" {
#include "sound.h"
#include "sound_blorb.h"
}

/*----------------------
 | SlicePcm
 | Description: A pre-loaded 8-bit mono PCM slice handed to SRL's PCM machinery.
 |   IPcmFile's fields are protected, so this subclass fills them via set(); the
 |   sample buffer itself is owned by the caller, not by this object.
 | Author: suinevere
 ----------------------*/
class SlicePcm : public SRL::Sound::Pcm::IPcmFile {
public:
    void set(int8_t* d, uint32_t n, uint16_t r) {
        data = d; dataSize = n; mode = _Mono; depth = _PCM8Bit; sampleRate = r;
    }
};

/*----------------------
 | NSLOT / SND_FPS / SND_LEAD
 | Description: NSLOT matches SRL's four PCM channels. SND_FPS is the Synchronize
 |   cadence (NTSC) used as the loop timing base. SND_LEAD is how many frames a
 |   loop's next copy starts before the current one ends, so the seam overlaps.
 | Author: suinevere
 ----------------------*/
#define NSLOT 4
#define SND_FPS  60
#define SND_LEAD 3

/*----------------------
 | Slot
 | Description: One active effect. A looping sound plays "ping-pong" across two
 |   channels to avoid the ~1-frame silent gap a same-channel re-trigger leaves:
 |   `channel` is the current (leading) channel and `channel2` the previous one
 |   still finishing its tail; one-shots use `channel` only (channel2 = -1).
 |   number is the Z sound number (0 = free), loops marks a looping effect, vol is
 |   the playback volume reused for the loop hand-off, and period/countdown time
 |   the hand-off in frames (loops only).
 | Author: suinevere
 ----------------------*/
struct Slot {
    int      number;
    int      loops;
    int8_t*  buf;
    SlicePcm pcm;
    uint8_t  vol;
    int      channel;
    int      channel2;
    int      period;
    int      countdown;
};

/*----------------------
 | g_blb / g_have / g_enabled / g_slot
 | Description: Engine state: the current .BLB filename, whether its index loaded,
 |   the Options on/off toggle, and the NSLOT active-effect slots.
 | Author: suinevere
 ----------------------*/
static char  g_blb[16];
static int   g_have;
static int   g_enabled = 1;
static Slot  g_slot[NSLOT];

/*----------------------
 | SliceCache / g_cache
 | Description: Persistent PCM slice cache: each sound number's slice is loaded
 |   from the CD once and kept for the game's lifetime, so re-triggers and the
 |   loop ping-pong never re-read the CD.
 | Author: suinevere
 ----------------------*/
#define NCACHE 8
struct SliceCache { int number; int8_t* buf; uint32_t play; unsigned short rate; };
static SliceCache g_cache[NCACHE];

#define CD_SECTOR 2048

/*----------------------
 | cd_reader
 | Description: Random-access read of `len` bytes at byte offset `off`, the read
 |   callback the Blorb parser uses. SRL's Cd::File Seek() does not reposition
 |   on-device (every Read starts at byte 0), so this uses sector-addressed
 |   LoadBytes: it loads the sector span covering [off, off+len) into a scratch
 |   buffer and copies out the sub-range. `len` is always small (<= the parser
 |   window), so two sectors suffice. Retries the flaky first-access read.
 | Author: suinevere
 | Dependencies: SRL (Cd::File)
 | Globals: g_blb, g_secbuf
 | Params: off -- byte offset; len -- bytes to read; out -- destination
 | Returns: 1 on success, 0 on failure or an over-large request
 ----------------------*/
static unsigned char g_secbuf[CD_SECTOR * 2];
static int cd_reader(unsigned int off, unsigned int len, unsigned char* out) {
    if (len == 0) return 0;
    unsigned int sec    = off / CD_SECTOR;
    unsigned int secoff = off % CD_SECTOR;
    unsigned int rbytes = ((secoff + len + CD_SECTOR - 1) / CD_SECTOR) * CD_SECTOR;
    if (rbytes > sizeof g_secbuf) return 0;
    for (int attempt = 0; attempt < 60; attempt++) {
        SRL::Cd::File f(g_blb);
        int32_t got = f.LoadBytes((size_t) sec, (int32_t) rbytes, g_secbuf);
        if (got >= (int32_t)(secoff + len)) {
            for (unsigned i = 0; i < len; i++) out[i] = g_secbuf[secoff + i];
            return 1;
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    return 0;
}

/*----------------------
 | load_slice
 | Description: Reads a full PCM sample into a fresh High Work RAM buffer using the
 |   same sector-addressed load as cd_reader (Seek() is broken on-device): it reads
 |   the sector span covering [off, off+len) straight into the buffer, shifts the
 |   sample down to the buffer start, then pads to slPCMOn's 0x900 minimum with
 |   silence. Retries the flaky first-access read.
 | Author: suinevere
 | Dependencies: SRL (Cd::File, Memory)
 | Globals: g_blb
 | Params: off -- byte offset of the sample; len -- sample length in bytes
 | Returns: the allocated buffer (caller/cache owns it), or nullptr on failure
 ----------------------*/
static int8_t* load_slice(unsigned int off, unsigned int len) {
    uint32_t play   = len < 0x900 ? 0x900 : len;
    unsigned int sec    = off / CD_SECTOR;
    unsigned int secoff = off % CD_SECTOR;
    uint32_t rbytes = ((secoff + len + CD_SECTOR - 1) / CD_SECTOR) * CD_SECTOR;
    uint32_t bufsz  = rbytes > play ? rbytes : play;
    int8_t* b = (int8_t*) SRL::Memory::HighWorkRam::Malloc(bufsz);
    if (!b) return nullptr;
    for (int attempt = 0; attempt < 60; attempt++) {
        SRL::Cd::File f(g_blb);
        int32_t got = f.LoadBytes((size_t) sec, (int32_t) rbytes, (uint8_t*) b);
        if (got >= (int32_t)(secoff + len)) {
            if (secoff) for (uint32_t i = 0; i < len; i++) b[i] = b[secoff + i];
            for (uint32_t i = len; i < play; i++) b[i] = 0;
            return b;
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    SRL::Memory::Free(b);
    return nullptr;
}

/*----------------------
 | free_slot
 | Description: Stops a slot's channel(s) and returns it to the free state. The
 |   `s.number != 0` guard matters for g_slot's static zero-init: a zeroed Slot has
 |   channel == 0 (a valid-looking index, not the -1 sentinel sound_init assigns),
 |   so a bare "channel >= 0" check would issue a spurious StopSound(0) on an
 |   unplayed slot the first time teardown runs. number is nonzero only while a
 |   slot actually holds a live channel/buffer. StopSound is the public wrapper for
 |   slPCMOff (Channels[] is private in srl_sound.hpp). Frees the slot's own buffer
 |   only -- cached buffers are owned by g_cache and freed by sound_stop_all.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm, Memory)
 | Globals: N/A (operates on the passed slot)
 | Params: s -- the slot to free
 | Returns: N/A
 ----------------------*/
static void free_slot(Slot& s) {
    if (s.number != 0) {
        if (s.channel  >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel);
        if (s.channel2 >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel2);
    }
    if (s.buf) { SRL::Memory::Free(s.buf); s.buf = nullptr; }
    s.number = 0; s.channel = -1; s.channel2 = -1; s.loops = 0;
}

/*----------------------
 | sound_init
 | Description: Points the engine at a game's .BLB and opens its Blorb index.
 |   Tears down any slots a previous game left active first: without this, a
 |   second call (game switch) with channels still playing would orphan their
 |   HighWorkRam buffers and leave looping sounds running with no slot to stop
 |   them. Safe on the first, statically zeroed call too (see free_slot's guard).
 | Author: suinevere
 | Dependencies: sound_blorb.h (sound_blorb_open)
 | Globals: g_slot, g_have, g_blb
 | Params: blbfile -- the .BLB filename, or NULL to run with no effects
 | Returns: N/A
 ----------------------*/
extern "C" void sound_init(const char* blbfile) {
    sound_stop_all();
    for (int i = 0; i < NSLOT; i++) {
        g_slot[i].number = 0; g_slot[i].channel = -1; g_slot[i].channel2 = -1; g_slot[i].buf = nullptr;
    }
    g_have = 0; g_blb[0] = '\0';
    if (!blbfile) return;
    int j = 0; for (; blbfile[j] && j < 15; j++) g_blb[j] = blbfile[j]; g_blb[j] = '\0';
    if (sound_blorb_open(cd_reader) > 0) g_have = 1;
}

/*----------------------
 | sound_stop_all
 | Description: Frees every active slot and every cached slice, releasing all PCM
 |   buffers. Called on teardown, a game switch, mute, and soft reset.
 | Author: suinevere
 | Dependencies: SRL (Memory)
 | Globals: g_slot, g_cache
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void sound_stop_all(void) {
    for (int i = 0; i < NSLOT; i++) free_slot(g_slot[i]);
    for (int i = 0; i < NCACHE; i++) {
        if (g_cache[i].buf) { SRL::Memory::Free(g_cache[i].buf); g_cache[i].buf = nullptr; }
        g_cache[i].number = 0;
    }
}

/*----------------------
 | sound_has_audio
 | Description: True when a .BLB effect index is loaded; gates the PCM rows in
 |   Sound Options.
 | Author: suinevere
 ----------------------*/
extern "C" int sound_has_audio(void) { return g_have; }

/*----------------------
 | sound_set_enabled
 | Description: Toggles effects on/off from Options, stopping all active sounds
 |   when turned off.
 | Author: suinevere
 ----------------------*/
extern "C" void sound_set_enabled(int on) {
    g_enabled = on;
    if (!on) sound_stop_all();
}

/*----------------------
 | g_level / sound_set_level
 | Description: PCM output level 0..7 (0 = silence), scaling effect volume; the
 |   default matches the Options PCM slider. Level 0 also disables playback and
 |   stops all sounds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_level, g_enabled
 | Params: level -- requested level, clamped to 0..7
 | Returns: N/A
 ----------------------*/
static int g_level = 4;
extern "C" void sound_set_level(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    g_enabled = (level > 0) ? 1 : 0;
    if (!g_enabled) sound_stop_all();
}

/*----------------------
 | cached_slice
 | Description: Returns a sound number's PCM slice via the persistent cache: one
 |   CD read per number, reused thereafter (freed by sound_stop_all). On a miss it
 |   loads the slice and records it in a free cache entry.
 | Author: suinevere
 | Dependencies: SRL (via load_slice)
 | Globals: g_cache
 | Params: number -- Z sound number; off/len -- its byte range; play_out --
 |   receives the padded playable size; rate -- sample rate to cache
 | Returns: the slice buffer (cache-owned), or nullptr on load failure
 ----------------------*/
static int8_t* cached_slice(int number, unsigned int off, unsigned int len,
                            uint32_t* play_out, unsigned short rate) {
    for (int i = 0; i < NCACHE; i++)
        if (g_cache[i].number == number && g_cache[i].buf) { *play_out = g_cache[i].play; return g_cache[i].buf; }
    int8_t* buf = load_slice(off, len);
    if (!buf) return nullptr;
    uint32_t play = len < 0x900 ? 0x900 : len;
    for (int i = 0; i < NCACHE; i++)
        if (g_cache[i].number == 0) {
            g_cache[i].number = number; g_cache[i].buf = buf;
            g_cache[i].play = play; g_cache[i].rate = rate; break;
        }
    *play_out = play; return buf;
}

/*----------------------
 | saturn_sound_effect
 | Description: The Z-machine sound_effect hook. Ignores everything when effects
 |   are off or no .BLB is loaded. effect 3/4 (stop/finish) frees any slot holding
 |   this number; effect 1 (prepare) is a no-op (on-demand load is fast enough);
 |   only effect 2 (start) plays. An already-active looping sound is left alone; a
 |   start with no free slot is dropped. Otherwise it takes a free slot, plays the
 |   cached slice at a volume derived from the Z volume and the PCM level (floored
 |   at 1 so it is audible), and for a looping sound computes the per-copy channel
 |   lifetime in frames and arms the ping-pong countdown.
 | Author: suinevere
 | Dependencies: sound_blorb.h (sound_blorb_get), SRL (Sound::Pcm)
 | Globals: g_enabled, g_have, g_slot, g_level
 | Params: number -- Z sound number; effect -- 1 prepare/2 start/3 stop/4 finish;
 |   volume -- Z volume (255 or <=0 means default)
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_sound_effect(int number, int effect, int volume) {
    if (!g_enabled || !g_have) return;
    unsigned int off, len; unsigned short rate; int loops;
    if (!sound_blorb_get(number, &off, &len, &rate, &loops)) return;

    if (effect == 3 || effect == 4) {
        for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number) free_slot(g_slot[i]);
        return;
    }
    if (effect != 2 && effect != 1) return;
    if (effect == 1) return;

    for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number && g_slot[i].channel >= 0) return;

    int free = -1; for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == 0) { free = i; break; }
    if (free < 0) return;

    uint32_t play = 0;
    int8_t* buf = cached_slice(number, off, len, &play, rate);
    if (!buf) return;
    Slot& s = g_slot[free];
    s.number = number; s.loops = loops; s.buf = nullptr;
    s.channel2 = -1;
    s.pcm.set(buf, play, rate);
    s.vol = (volume == 255 || volume <= 0) ? 100 : (uint8_t)((volume > 8 ? 8 : volume) * 127 / 8);
    s.vol = (uint8_t) (((int) s.vol) * g_level / 7);
    if (s.vol == 0) s.vol = 1;
    s.channel = s.pcm.Play(s.vol);
    if (s.channel < 0) { free_slot(s); return; }
    if (loops) {
        uint32_t p = ((uint32_t) play * SND_FPS) / (rate ? rate : 1);
        s.period = (int) p;
        if (s.period < SND_LEAD + 2) s.period = SND_LEAD + 2;
        s.countdown = s.period - SND_LEAD;
    }
}

/*----------------------
 | sound_service
 | Description: One frame of slot upkeep, called every game frame. Reaps finished
 |   one-shots. For a looping slot it counts down and, a few frames before the
 |   leading channel ends, starts the next copy on a free channel (retiring the
 |   old tail) so a fresh copy is already sounding at the seam. A safety net
 |   restarts the leading channel in place if the hand-off fell behind (no free
 |   channel, or the game held the CPU past it) and it already went silent, so the
 |   loop recovers rather than staying dead.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm)
 | Globals: g_enabled, g_slot
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void sound_service(void) {
    if (!g_enabled) return;
    for (int i = 0; i < NSLOT; i++) {
        Slot& s = g_slot[i];
        if (s.number == 0) continue;

        if (!s.loops) {
            if (s.channel >= 0 && SRL::Sound::Pcm::IsChannelFree((uint8_t) s.channel))
                free_slot(s);
            continue;
        }

        if (s.countdown > 0) s.countdown--;
        if (s.countdown <= 0) {
            int nb = s.pcm.Play(s.vol);
            if (nb >= 0) {
                if (s.channel2 >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel2);
                s.channel2 = s.channel;
                s.channel  = nb;
            }
            s.countdown = s.period - SND_LEAD;
            if (s.countdown < 1) s.countdown = 1;
        }
        if (s.channel >= 0 && SRL::Sound::Pcm::IsChannelFree((uint8_t) s.channel)) {
            s.pcm.PlayOnChannel((uint8_t) s.channel, s.vol);
            s.countdown = s.period - SND_LEAD;
            if (s.countdown < 1) s.countdown = 1;
        }
    }
}
