#include "sound_blorb.h"

#define MAXSND 32

typedef struct {
    int number;
    unsigned int off, len;   /* SSND data offset + length within the file */
    unsigned short rate;
    int loops;
} SndEntry;

static SndEntry g_snd[MAXSND];
static int      g_count;

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

/* Decode an 80-bit IEEE extended float (AIFF COMM sample rate) to an int Hz. */
static unsigned short ext_rate(const unsigned char* p) {
    int exp = (int)(be16(p) & 0x7fff);
    /* Only the top 32 bits of the 64-bit mantissa matter for sane rates. */
    unsigned int hi = be32(p + 2);
    if (exp == 0) return 0;
    /* value = hi * 2^(exp-16383-31) */
    int shift = exp - 16383 - 31;
    double v = (double)hi;
    while (shift > 0) { v *= 2.0; shift--; }
    while (shift < 0) { v *= 0.5; shift++; }
    return (unsigned short)(v + 0.5);
}

/* Parse one sound's FORM…AIFF at file offset `start`: fill data off/len/rate. */
static int parse_sound(sound_read_fn read, unsigned int start,
                       unsigned int* off, unsigned int* len, unsigned short* rate) {
    unsigned char b[160];
    if (!read(start, sizeof b, b)) return 0;
    if (!tag(b, "FORM") || !tag(b + 8, "AIFF")) return 0;
    unsigned int formlen = be32(b + 4);
    unsigned int p = 12;                 /* first chunk within the read window */
    unsigned int endw = sizeof b;
    *rate = 0; *off = 0; *len = 0;
    while (p + 8 <= endw && p < 8 + formlen) {
        const unsigned char* c = b + p;
        unsigned int clen = be32(c + 4);
        if (tag(c, "COMM")) *rate = ext_rate(c + 16);        /* rate at COMM+16 */
        else if (tag(c, "SSND")) { *off = start + p + 16; *len = clen - 8; }
        p += 8 + clen + (clen & 1);
        if (*rate && *off) break;        /* got both */
    }
    return (*off != 0 && *rate != 0);
}

int sound_blorb_open(sound_read_fn read) {
    unsigned char hdr[12];
    g_count = 0;
    if (!read || !read(0, 12, hdr)) return 0;
    if (!tag(hdr, "FORM") || !tag(hdr + 8, "IFRS")) return 0;

    /* RIdx is the first chunk after the FORM/IFRS header, at offset 12. */
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

    /* The Loop chunk follows the last resource (highest start). Find it there. */
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

void sound_blorb_close(void) { g_count = 0; }
