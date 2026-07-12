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

// CD reader for the parser: open the .BLB and read a slice.
//
// Deviation from the brief: SRL::Cd::File::LoadBytes()'s first parameter is
// documented in srl_cd.hpp as "sectorOffset - Number of sectors to skip at
// the start" (it forwards straight to GFS_Load's `ofs`, which is sector
// granularity), not a byte offset. sound_blorb_get() hands back byte offsets
// into the .BLB (per sound_blorb.h), so passing them straight to LoadBytes
// would read from the wrong place. Cd::File::Seek()/Read() are documented
// (and exercised elsewhere in SRL, e.g. WaveSound) as byte-precise, so we use
// Open()+Seek()+Read() instead for both the parser's reads and the slice load.
static int cd_reader(unsigned int off, unsigned int len, unsigned char* out) {
    SRL::Cd::File f(g_blb);
    if (!f.Exists() || !f.Open()) return 0;
    bool ok = f.Seek((int32_t) off) == (int32_t) off &&
              f.Read((int32_t) len, out) == (int32_t) len;
    f.Close();
    return ok ? 1 : 0;
}

static int8_t* load_slice(unsigned int off, unsigned int len) {
    // slPCMOn won't play samples shorter than 0x900; pad with silence.
    uint32_t n = len < 0x900 ? 0x900 : len;
    int8_t* b = (int8_t*) SRL::Memory::HighWorkRam::Malloc(n);
    if (!b) return nullptr;
    for (uint32_t i = len; i < n; i++) b[i] = 0;

    SRL::Cd::File f(g_blb);
    bool ok = f.Exists() && f.Open() &&
              f.Seek((int32_t) off) == (int32_t) off &&
              f.Read((int32_t) len, (uint8_t*) b) == (int32_t) len;
    f.Close();
    if (!ok) { SRL::Memory::Free(b); return nullptr; }
    return b;
}

static void free_slot(Slot& s) {
    // Deviation from the brief: SRL::Sound::Pcm::Channels[] is a private
    // member of SRL::Sound::Pcm (srl_sound.hpp), so it and raw slPCMOff()
    // aren't reachable from here. Pcm::StopSound(channel) is the public
    // wrapper that does the same slPCMOff(&Channels[channel]) call.
    if (s.channel >= 0) SRL::Sound::Pcm::StopSound((uint8_t) s.channel);
    if (s.buf) { SRL::Memory::Free(s.buf); s.buf = nullptr; }
    s.number = 0; s.channel = -1; s.loops = 0;
}

extern "C" void sound_init(const char* blbfile) {
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
    if (effect == 1) return;                     // prepare: on-demand load is fast enough

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
