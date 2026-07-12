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

// --- temporary debug instrumentation: observe sound_effect activity ---
// outcome: -1 none, 0 played, 1 disabled, 2 no-data(g_have=0), 3 unknown-number,
// 4 load-fail, 5 no-slot, 6 stop/finish, 7 prepare, 8 other-effect,
// 9 already-active, 10 no-channel.
static int g_dbg_calls = 0, g_dbg_num = 0, g_dbg_eff = 0, g_dbg_vol = 0, g_dbg_out = -1;
extern "C" void sound_debug_get(int* calls, int* num, int* eff, int* vol, int* out) {
    if (calls) *calls = g_dbg_calls;
    if (num)   *num   = g_dbg_num;
    if (eff)   *eff   = g_dbg_eff;
    if (vol)   *vol   = g_dbg_vol;
    if (out)   *out   = g_dbg_out;
}

// Second diagnostic: why sound_init failed. ssz=SectorSize seen, op=Open ok,
// rd=Read ok, ns=sound_blorb_open() return, h0..3=first header bytes (FORM?).
static int g_dbg_ssz = -99, g_dbg_op = -1, g_dbg_rd = -1, g_dbg_ns = -99;
static unsigned char g_dbg_h[4] = {0, 0, 0, 0};
extern "C" const char* sound_debug_name(void) { return g_blb; }
extern "C" void sound_debug2(int* ssz, int* op, int* rd, int* ns, unsigned char* h4) {
    if (ssz) *ssz = g_dbg_ssz;
    if (op)  *op  = g_dbg_op;
    if (rd)  *rd  = g_dbg_rd;
    if (ns)  *ns  = g_dbg_ns;
    if (h4)  { for (int i = 0; i < 4; i++) h4[i] = g_dbg_h[i]; }
}

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
    // The CD file stat (GFS_GetFileSize, used by the Cd::File ctor) can report an
    // uninitialized size on first access -- the story loader in main.cxx works
    // around this by retrying until the size is sane. Mirror that here: retry
    // until the file reports 2048-byte data sectors and opens, then seek+read.
    for (int attempt = 0; attempt < 300; attempt++) {
        SRL::Cd::File f(g_blb);
        int32_t ssz = f.Size.SectorSize;
        if (off == 0 && attempt == 0) g_dbg_ssz = (int) ssz;   // initial stat
        if (ssz == 2048 && f.Open()) {
            if (off == 0) g_dbg_op = 1;
            bool ok = f.Seek((int32_t) off) == (int32_t) off &&
                      f.Read((int32_t) len, out) == (int32_t) len;
            f.Close();
            if (ok) {
                if (off == 0) { g_dbg_rd = 1;
                    for (unsigned i = 0; i < 4 && i < len; i++) g_dbg_h[i] = out[i]; }
                return 1;
            }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    return 0;
}

static int8_t* load_slice(unsigned int off, unsigned int len) {
    // slPCMOn won't play samples shorter than 0x900; pad with silence.
    uint32_t n = len < 0x900 ? 0x900 : len;
    int8_t* b = (int8_t*) SRL::Memory::HighWorkRam::Malloc(n);
    if (!b) return nullptr;
    for (uint32_t i = len; i < n; i++) b[i] = 0;

    for (int attempt = 0; attempt < 60; attempt++) {
        SRL::Cd::File f(g_blb);
        if (f.Size.SectorSize == 2048 && f.Open()) {
            bool ok = f.Seek((int32_t) off) == (int32_t) off &&
                      f.Read((int32_t) len, (uint8_t*) b) == (int32_t) len;
            f.Close();
            if (ok) return b;
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
    g_dbg_ns = sound_blorb_open(cd_reader);
    if (g_dbg_ns > 0) g_have = 1;
}

extern "C" void sound_stop_all(void) {
    for (int i = 0; i < NSLOT; i++) free_slot(g_slot[i]);
}

extern "C" void sound_set_enabled(int on) {
    g_enabled = on;
    if (!on) sound_stop_all();
}

extern "C" void saturn_sound_effect(int number, int effect, int volume) {
    g_dbg_calls++; g_dbg_num = number; g_dbg_eff = effect; g_dbg_vol = volume; g_dbg_out = -1;
    if (!g_enabled) { g_dbg_out = 1; return; }
    if (!g_have)    { g_dbg_out = 2; return; }
    unsigned int off, len; unsigned short rate; int loops;
    if (!sound_blorb_get(number, &off, &len, &rate, &loops)) { g_dbg_out = 3; return; }

    if (effect == 3 || effect == 4) {           // stop / finish
        for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number) free_slot(g_slot[i]);
        g_dbg_out = 6; return;
    }
    if (effect != 2 && effect != 1) { g_dbg_out = 8; return; }  // only start / prepare handled
    if (effect == 1) { g_dbg_out = 7; return; }                 // prepare: on-demand load is fast enough

    // start: if this looping sound is already active, leave it be.
    for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number && g_slot[i].channel >= 0) { g_dbg_out = 9; return; }

    int free = -1; for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == 0) { free = i; break; }
    if (free < 0) { g_dbg_out = 5; return; }      // all channels busy: drop it

    int8_t* buf = load_slice(off, len);
    if (!buf) { g_dbg_out = 4; return; }
    Slot& s = g_slot[free];
    s.number = number; s.loops = loops; s.buf = buf;
    s.pcm.set(buf, len < 0x900 ? 0x900 : len, rate);
    uint8_t vol = (volume == 255 || volume <= 0) ? 100 : (uint8_t)((volume > 8 ? 8 : volume) * 127 / 8);
    s.channel = s.pcm.Play(vol);
    if (s.channel < 0) { free_slot(s); g_dbg_out = 10; return; }  // no channel: undo
    g_dbg_out = 0;                                 // playing
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
