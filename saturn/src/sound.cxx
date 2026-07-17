#include <srl.hpp>
extern "C" {
#include "sound.h"
#include "sound_blorb.h"
}

// A pre-loaded PCM slice we can hand to SRL's PCM machinery. IPcmFile's fields
// are protected, so a subclass fills them; we own the buffer ourselves.
class SlicePcm : public SRL::Sound::Pcm::IPcmFile {
public:
    void set(int8_t* d, uint32_t n, uint16_t r) {
        data = d; dataSize = n; mode = _Mono; depth = _PCM8Bit; sampleRate = r;
    }
};

#define NSLOT 4                     // matches SRL's 4 PCM channels
#define SND_FPS  60                 // Synchronize cadence (NTSC); loop timing base
#define SND_LEAD 3                  // frames of overlap before a loop channel ends

// A looping sound plays "ping-pong": to avoid the ~1-frame silent gap that a
// same-channel re-trigger leaves, we start the sample on a second channel a few
// frames before the current one ends, so the seam overlaps instead of dropping
// out. `channel` is the current (leading) channel; `channel2` is the previous
// one still finishing its tail. One-shots use `channel` only (channel2 = -1).
struct Slot {
    int      number;               // Z sound number, 0 = free
    int      loops;                // 1 = looping (ping-pong)
    int8_t*  buf;
    SlicePcm pcm;
    uint8_t  vol;                  // playback volume (re-used for loop hand-off)
    int      channel;              // leading channel, -1 = none
    int      channel2;             // loop's previous (overlapping) channel, -1 = none
    int      period;               // sample length in frames (loops only)
    int      countdown;            // frames until the next hand-off (loops only)
};

static char  g_blb[16];
static int   g_have;               // 1 if the index loaded
static int   g_enabled = 1;        // Options toggle
static Slot  g_slot[NSLOT];

// Persistent PCM slice cache (see cached_slice below): each sound number's slice
// is loaded from the CD once and kept for the game's lifetime, so re-triggers and
// the loop's ping-pong never re-read the CD (which would interrupt CD-DA music).
#define NCACHE 8
struct SliceCache { int number; int8_t* buf; uint32_t play; unsigned short rate; };
static SliceCache g_cache[NCACHE];

#define CD_SECTOR 2048
// Random-access read of `len` bytes at byte offset `off`. SRL's Cd::File Seek()
// does NOT reposition on-device (every Read starts at byte 0), so we use the
// sector-addressed LoadBytes(sectorOffset, size, dest) instead: load the whole
// sector span covering [off, off+len) into a scratch buffer, then copy out the
// sub-range. `len` here is always small (<= parser window), so 2 sectors suffice.
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

static int8_t* load_slice(unsigned int off, unsigned int len) {
    // Same sector-addressed load as cd_reader (Seek() is broken on-device), but
    // sized for a full PCM slice. Read the sector span covering [off, off+len)
    // straight into the playback buffer, shift the sample down to the buffer
    // start, then pad to slPCMOn's 0x900 minimum with silence.
    uint32_t play   = len < 0x900 ? 0x900 : len;      // playable size (padded)
    unsigned int sec    = off / CD_SECTOR;
    unsigned int secoff = off % CD_SECTOR;
    uint32_t rbytes = ((secoff + len + CD_SECTOR - 1) / CD_SECTOR) * CD_SECTOR;
    uint32_t bufsz  = rbytes > play ? rbytes : play;  // hold the raw read AND padded PCM
    int8_t* b = (int8_t*) SRL::Memory::HighWorkRam::Malloc(bufsz);
    if (!b) return nullptr;
    for (int attempt = 0; attempt < 60; attempt++) {
        SRL::Cd::File f(g_blb);
        int32_t got = f.LoadBytes((size_t) sec, (int32_t) rbytes, (uint8_t*) b);
        if (got >= (int32_t)(secoff + len)) {
            if (secoff) for (uint32_t i = 0; i < len; i++) b[i] = b[secoff + i];  // shift to start
            for (uint32_t i = len; i < play; i++) b[i] = 0;                       // pad silence
            return b;
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    SRL::Memory::Free(b);
    return nullptr;
}

static void free_slot(Slot& s) {
    // Pcm::StopSound(channel) is the public wrapper for slPCMOff(&Channels[ch])
    // (Channels[] itself is private in srl_sound.hpp).
    //
    // The s.number != 0 guard matters for g_slot's static zero-init state:
    // statically zero-initialized Slots have channel == 0 (a valid-looking
    // channel index, not the sentinel -1 that sound_init() normally assigns),
    // so a bare "channel >= 0" check would issue a spurious StopSound(0) on
    // an unplayed slot the very first time teardown runs. number is only ever
    // non-zero while a slot actually holds a live channel/buffer.
    if (s.number != 0) {
        if (s.channel  >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel);
        if (s.channel2 >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel2);
    }
    if (s.buf) { SRL::Memory::Free(s.buf); s.buf = nullptr; }
    s.number = 0; s.channel = -1; s.channel2 = -1; s.loops = 0;
}

extern "C" void sound_init(const char* blbfile) {
    // Tear down any slots a previous game left active before resetting
    // bookkeeping: without this, a second sound_init() call (game switch)
    // while channels are still playing would orphan their HighWorkRam
    // buffers (never Free()'d) and leave looping sounds running on the SGL
    // channel with no slot left to re-trigger or stop them. Safe to call on
    // the very first, statically zero-initialized call too - see the
    // s.number != 0 guard in free_slot() above.
    sound_stop_all();
    for (int i = 0; i < NSLOT; i++) {
        g_slot[i].number = 0; g_slot[i].channel = -1; g_slot[i].channel2 = -1; g_slot[i].buf = nullptr;
    }
    g_have = 0; g_blb[0] = '\0';
    if (!blbfile) return;
    int j = 0; for (; blbfile[j] && j < 15; j++) g_blb[j] = blbfile[j]; g_blb[j] = '\0';
    if (sound_blorb_open(cd_reader) > 0) g_have = 1;
}

extern "C" void sound_stop_all(void) {
    for (int i = 0; i < NSLOT; i++) free_slot(g_slot[i]);
    for (int i = 0; i < NCACHE; i++) {
        if (g_cache[i].buf) { SRL::Memory::Free(g_cache[i].buf); g_cache[i].buf = nullptr; }
        g_cache[i].number = 0;
    }
}

extern "C" int sound_has_audio(void) { return g_have; }

extern "C" void sound_set_enabled(int on) {
    g_enabled = on;
    if (!on) sound_stop_all();
}

// PCM output level 0..7 (0 = silence). Scales effect volume; 0 disables playback.
static int g_level = 4;   // default matches the Options PCM slider default
extern "C" void sound_set_level(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    g_enabled = (level > 0) ? 1 : 0;
    if (!g_enabled) sound_stop_all();
}

// Load a slice via the persistent cache (declared up top): one CD read per sound
// number, reused thereafter. Freed by sound_stop_all().
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

extern "C" void saturn_sound_effect(int number, int effect, int volume) {
    if (!g_enabled || !g_have) return;
    unsigned int off, len; unsigned short rate; int loops;
    if (!sound_blorb_get(number, &off, &len, &rate, &loops)) return;

    if (effect == 3 || effect == 4) {           // stop / finish
        for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number) free_slot(g_slot[i]);
        return;
    }
    if (effect != 2 && effect != 1) return;      // only start / prepare handled
    if (effect == 1) return;                      // prepare: on-demand load is fast enough

    // start: if this looping sound is already active, leave it be.
    for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number && g_slot[i].channel >= 0) return;

    int free = -1; for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == 0) { free = i; break; }
    if (free < 0) return;                         // all channels busy: drop it

    uint32_t play = 0;
    int8_t* buf = cached_slice(number, off, len, &play, rate);
    if (!buf) return;
    Slot& s = g_slot[free];
    s.number = number; s.loops = loops; s.buf = nullptr;   // the cache owns the buffer
    s.channel2 = -1;
    s.pcm.set(buf, play, rate);
    s.vol = (volume == 255 || volume <= 0) ? 100 : (uint8_t)((volume > 8 ? 8 : volume) * 127 / 8);
    s.vol = (uint8_t) (((int) s.vol) * g_level / 7);   // apply the PCM output level (7 = full)
    if (s.vol == 0) s.vol = 1;
    s.channel = s.pcm.Play(s.vol);
    if (s.channel < 0) { free_slot(s); return; }  // no channel: undo
    if (loops) {
        // Frames the sample occupies a channel (8-bit mono -> 1 byte per sample).
        uint32_t p = ((uint32_t) play * SND_FPS) / (rate ? rate : 1);
        s.period = (int) p;
        if (s.period < SND_LEAD + 2) s.period = SND_LEAD + 2;  // guard very short loops
        s.countdown = s.period - SND_LEAD;
    }
}

extern "C" void sound_service(void) {
    if (!g_enabled) return;
    for (int i = 0; i < NSLOT; i++) {
        Slot& s = g_slot[i];
        if (s.number == 0) continue;

        if (!s.loops) {                     // one-shot: reap when it finishes
            if (s.channel >= 0 && SRL::Sound::Pcm::IsChannelFree((uint8_t) s.channel))
                free_slot(s);
            continue;
        }

        // Looping: ping-pong hand-off a few frames before the leading channel
        // ends, so a fresh copy is already sounding when this one goes silent.
        if (s.countdown > 0) s.countdown--;
        if (s.countdown <= 0) {
            int nb = s.pcm.Play(s.vol);     // start the next copy on a free channel
            if (nb >= 0) {
                if (s.channel2 >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel2);
                s.channel2 = s.channel;     // old leading becomes the finishing tail
                s.channel  = nb;            // new leading
            }
            s.countdown = s.period - SND_LEAD;
            if (s.countdown < 1) s.countdown = 1;
        }
        // Safety net: if we fell behind (e.g. no free channel, or the game held
        // the CPU past the hand-off) and the leading channel already went silent,
        // restart it in place so the loop recovers rather than staying dead.
        if (s.channel >= 0 && SRL::Sound::Pcm::IsChannelFree((uint8_t) s.channel)) {
            s.pcm.PlayOnChannel((uint8_t) s.channel, s.vol);
            s.countdown = s.period - SND_LEAD;
            if (s.countdown < 1) s.countdown = 1;
        }
    }
}
