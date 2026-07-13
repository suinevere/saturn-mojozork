/*----------------------
 | sound_blorb.c
 | Description: A minimal Blorb (IFRS) index reader for the game's .BLB sound
 |   file. Walks the resource index to find each Snd resource, parses its
 |   FORM/AIFF header for the PCM data offset/length and sample rate, and reads
 |   the trailing Loop chunk to mark which sounds loop. All disc access goes
 |   through a caller-supplied read callback, so this file has no platform
 |   dependency.
 | Author: suinevere
 | Dependencies: sound_blorb.h (sound_read_fn and the public API)
 ----------------------*/
#include "sound_blorb.h"

#define MAXSND 32

/*----------------------
 | SndEntry / g_snd / g_count
 | Description: The parsed sound table: per entry the Z sound number, the SSND PCM
 |   data offset/length within the file, the sample rate, and a loop flag. g_count
 |   is how many entries are valid.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int number;
    unsigned int off, len;
    unsigned short rate;
    int loops;
} SndEntry;

static SndEntry g_snd[MAXSND];
static int      g_count;

/*----------------------
 | be32 / be16 / tag
 | Description: Big-endian 32/16-bit reads and a 4-byte chunk-tag comparison, the
 |   primitives the IFF/AIFF parsing is built on.
 | Author: suinevere
 ----------------------*/
static unsigned int be32(const unsigned char* p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}
static unsigned int be16(const unsigned char* p) {
    return ((unsigned int)p[0] << 8) | p[1];
}
static int tag(const unsigned char* p, const char* t) {
    return p[0]==t[0] && p[1]==t[1] && p[2]==t[2] && p[3]==t[3];
}

/*----------------------
 | ext_rate
 | Description: Decodes an AIFF COMM 80-bit IEEE extended float (the sample rate)
 |   to an integer Hz. Only the top 32 bits of the mantissa matter for sane rates,
 |   so it scales `hi` by 2^(exp-16383-31) and rounds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- pointer to the 10-byte extended float
 | Returns: the sample rate in Hz (0 for a zero exponent)
 ----------------------*/
static unsigned short ext_rate(const unsigned char* p) {
    int exp = (int)(be16(p) & 0x7fff);
    unsigned int hi = be32(p + 2);
    if (exp == 0) return 0;
    int shift = exp - 16383 - 31;
    double v = (double)hi;
    while (shift > 0) { v *= 2.0; shift--; }
    while (shift < 0) { v *= 0.5; shift++; }
    return (unsigned short)(v + 0.5);
}

/*----------------------
 | parse_sound
 | Description: Parses one sound's FORM/AIFF at file offset `start` within a fixed
 |   160-byte read window, walking its chunks to read the COMM sample rate and the
 |   SSND PCM data offset (past SSND's 4-byte offset field) and length. Chunks are
 |   padded to even length. Stops once both rate and data offset are known.
 | Author: suinevere
 | Dependencies: N/A (reads via the callback)
 | Globals: N/A
 | Params: read -- the disc read callback; start -- FORM offset; off/len --
 |   receive the PCM data range; rate -- receives the sample rate
 | Returns: 1 when both a data offset and a rate were found, else 0
 ----------------------*/
static int parse_sound(sound_read_fn read, unsigned int start,
                       unsigned int* off, unsigned int* len, unsigned short* rate) {
    unsigned char b[160];
    if (!read(start, sizeof b, b)) return 0;
    if (!tag(b, "FORM") || !tag(b + 8, "AIFF")) return 0;
    unsigned int formlen = be32(b + 4);
    unsigned int p = 12;
    unsigned int endw = sizeof b;
    *rate = 0; *off = 0; *len = 0;
    while (p + 8 <= endw && p < 8 + formlen) {
        const unsigned char* c = b + p;
        unsigned int clen = be32(c + 4);
        if (tag(c, "COMM")) {
            if (p + 26 <= endw) *rate = ext_rate(c + 16);
        } else if (tag(c, "SSND")) {
            if (p + 16 <= endw) {
                *off = start + p + 16 + be32(c + 8);
                *len = clen - 8;
            }
        }
        p += 8 + clen + (clen & 1);
        if (*rate && *off) break;
    }
    return (*off != 0 && *rate != 0);
}

/*----------------------
 | sound_blorb_open
 | Description: Opens a Blorb file through the read callback and builds the sound
 |   table. Validates the FORM/IFRS header and the RIdx resource index, then for
 |   each "Snd " resource parses its AIFF into an entry (tracking the highest
 |   resource start). Finally it locates the Loop chunk that follows the last
 |   resource and flags every listed sound number as looping. Fills g_snd/g_count.
 | Author: suinevere
 | Dependencies: N/A (reads via the callback)
 | Globals: g_snd, g_count
 | Params: read -- the disc read callback
 | Returns: the number of sounds indexed (0 on a bad/absent file)
 ----------------------*/
int sound_blorb_open(sound_read_fn read) {
    unsigned char hdr[12];
    g_count = 0;
    if (!read || !read(0, 12, hdr)) return 0;
    if (!tag(hdr, "FORM") || !tag(hdr + 8, "IFRS")) return 0;

    unsigned char rh[8];
    if (!read(12, 8, rh) || !tag(rh, "RIdx")) return 0;
    unsigned char nbuf[4];
    if (!read(20, 4, nbuf)) return 0;
    int nres = (int)be32(nbuf);
    if (nres < 0 || nres > 64) return 0;

    unsigned int maxstart = 0;
    for (int i = 0; i < nres && g_count < MAXSND; i++) {
        unsigned char e[12];
        if (!read(24 + (unsigned)i * 12, 12, e)) return 0;
        if (!tag(e, "Snd ")) continue;
        int num = (int)be32(e + 4);
        unsigned int start = be32(e + 8);
        unsigned int off, len; unsigned short rate;
        if (parse_sound(read, start, &off, &len, &rate)) {
            g_snd[g_count].number = num;
            g_snd[g_count].off = off; g_snd[g_count].len = len;
            g_snd[g_count].rate = rate; g_snd[g_count].loops = 0;
            g_count++;
            if (start > maxstart) maxstart = start;
        }
    }

    unsigned char fh[8];
    if (maxstart && read(maxstart, 8, fh) && tag(fh, "FORM")) {
        unsigned int fl = be32(fh + 4);
        unsigned int lp = maxstart + 8 + fl + (fl & 1);
        unsigned char lh[8];
        if (read(lp, 8, lh) && tag(lh, "Loop")) {
            unsigned int llen = be32(lh + 4);
            int pairs = (int)(llen / 8);
            for (int i = 0; i < pairs; i++) {
                unsigned char pe[8];
                if (!read(lp + 8 + (unsigned)i * 8, 8, pe)) break;
                int num = (int)be32(pe);
                for (int j = 0; j < g_count; j++)
                    if (g_snd[j].number == num) g_snd[j].loops = 1;
            }
        }
    }
    return g_count;
}

/*----------------------
 | sound_blorb_get
 | Description: Looks up a sound by Z number and returns its PCM data range, rate,
 |   and loop flag.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_snd, g_count
 | Params: number -- Z sound number; off/len/rate/loops -- receive the entry
 | Returns: 1 if found, 0 otherwise
 ----------------------*/
int sound_blorb_get(int number, unsigned int* off, unsigned int* len,
                    unsigned short* rate, int* loops) {
    for (int i = 0; i < g_count; i++) {
        if (g_snd[i].number == number) {
            *off = g_snd[i].off; *len = g_snd[i].len;
            *rate = g_snd[i].rate; *loops = g_snd[i].loops;
            return 1;
        }
    }
    return 0;
}

/*----------------------
 | sound_blorb_close
 | Description: Drops the sound table (resets the count).
 | Author: suinevere
 ----------------------*/
void sound_blorb_close(void) { g_count = 0; }
