#include <stdio.h>
#include <stdlib.h>
#include "sound/sound_blorb.h"

static FILE* g_f;
static int reader(unsigned int off, unsigned int len, unsigned char* buf) {
    if (fseek(g_f, (long)off, SEEK_SET) != 0) return 0;
    return fread(buf, 1, len, g_f) == len;
}
#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

int main(int argc, char** argv) {
    int fails = 0;
    g_f = fopen(argv[1], "rb");
    if (!g_f) { printf("cannot open %s\n", argv[1]); return 2; }

    int n = sound_blorb_open(reader);
    CHECK(n == 14);

    unsigned int off, len; unsigned short rate; int loops;
    CHECK(sound_blorb_get(3, &off, &len, &rate, &loops) == 1);
    CHECK(off == 362 && len == 49756 && rate == 9676 && loops == 0);

    CHECK(sound_blorb_get(4, &off, &len, &rate, &loops) == 1);
    CHECK(off == 50172 && len == 20520 && rate == 12902 && loops == 1);

    CHECK(sound_blorb_get(18, &off, &len, &rate, &loops) == 1 && loops == 1);
    CHECK(sound_blorb_get(5, &off, &len, &rate, &loops) == 0);   // no Snd #5

    sound_blorb_close();
    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
