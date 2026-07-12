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
struct Slot {
    int      number;               // Z sound number, 0 = free
    int      channel;              // SRL PCM channel, -1 = none
    int      loops;
    int8_t*  buf;
    SlicePcm pcm;
};

static char  g_blb[16];
static int   g_have;               // 1 if the index loaded
static int   g_enabled = 1;        // Options toggle
static Slot  g_slot[NSLOT];

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
    // Deviation from the brief: SRL::Sound::Pcm::Channels[] is a private
    // member of SRL::Sound::Pcm (srl_sound.hpp), so it and raw slPCMOff()
    // aren't reachable from here. Pcm::StopSound(channel) is the public
    // wrapper that does the same slPCMOff(&Channels[channel]) call.
    //
    // The s.number != 0 guard matters for g_slot's static zero-init state:
    // statically zero-initialized Slots have channel == 0 (a valid-looking
    // channel index, not the sentinel -1 that sound_init() normally assigns),
    // so a bare "channel >= 0" check would issue a spurious StopSound(0) on
    // an unplayed slot the very first time teardown runs. number is only
    // ever non-zero while a slot is actually holding a live channel/buffer
    // (see saturn_sound_effect), so gating on it keeps this safe on zeroed
    // slots without changing behavior for any slot that was actually used.
    if (s.number != 0 && s.channel >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel);
    if (s.buf) { SRL::Memory::Free(s.buf); s.buf = nullptr; }
    s.number = 0; s.channel = -1; s.loops = 0;
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
    for (int i = 0; i < NSLOT; i++) { g_slot[i].number = 0; g_slot[i].channel = -1; g_slot[i].buf = nullptr; }
    g_have = 0; g_blb[0] = '\0';
    if (!blbfile) return;
    int j = 0; for (; blbfile[j] && j < 15; j++) g_blb[j] = blbfile[j]; g_blb[j] = '\0';
    if (sound_blorb_open(cd_reader) > 0) g_have = 1;
}

extern "C" void sound_stop_all(void) {
    for (int i = 0; i < NSLOT; i++) free_slot(g_slot[i]);
}

extern "C" void sound_set_enabled(int on) {
    g_enabled = on;
    if (!on) sound_stop_all();
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

    int8_t* buf = load_slice(off, len);
    if (!buf) return;
    Slot& s = g_slot[free];
    s.number = number; s.loops = loops; s.buf = buf;
    s.pcm.set(buf, len < 0x900 ? 0x900 : len, rate);
    uint8_t vol = (volume == 255 || volume <= 0) ? 100 : (uint8_t)((volume > 8 ? 8 : volume) * 127 / 8);
    s.channel = s.pcm.Play(vol);
    if (s.channel < 0) free_slot(s);              // no channel: undo
}

extern "C" void sound_service(void) {
    if (!g_enabled) return;
    for (int i = 0; i < NSLOT; i++) {
        Slot& s = g_slot[i];
        if (s.number == 0 || s.channel < 0) continue;
        // Deviation from the brief: same reason as free_slot() above -
        // Pcm::IsChannelFree(channel) is the public wrapper for
        // !slPCMStat(&Channels[channel]), since Channels[] is private.
        if (SRL::Sound::Pcm::IsChannelFree((uint8_t) s.channel)) {   // finished
            if (s.loops) s.pcm.PlayOnChannel((uint8_t) s.channel, 100);  // re-trigger
            else free_slot(s);
        }
    }
}
