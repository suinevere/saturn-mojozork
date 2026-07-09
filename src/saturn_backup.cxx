#include <srl.hpp>
#include <sega_bup.h>
#include "saturn_backup.h"

// Driver workspaces (sizes per Jo Engine's backup init: 16 KB library + 8 KB work).
static uint32_t  bup_lib[16384 / 4];
static uint32_t  bup_work[8192 / 4];
static BupConfig bup_cfg[3];
static int       bup_ready = 0;

static void copy_name(uint8_t out[12], const char *name) {
    int i;
    for (i = 0; i < 12; i++) out[i] = 0;
    for (i = 0; i < 11 && name[i]; i++) out[i] = (uint8_t) name[i];
}

extern "C" void saturn_bup_init(void) {
    if (bup_ready) return;
    BUP_Init((uint32_t *) bup_lib, bup_work, bup_cfg);
    bup_ready = 1;
}

// Present if a device answers BUP_Stat (formatted or not); absent -> BUP_NON/error.
// Does NOT format here, so a mere presence check never wipes a cartridge.
extern "C" int saturn_bup_present(int device) {
    if (!bup_ready) saturn_bup_init();
    BupStat st;
    int32_t s = BUP_Stat((uint32_t) device, 1, &st);
    return (s == 0 || s == BUP_UNFORMAT) ? 1 : 0;
}

extern "C" int saturn_bup_write(int device, const char *name, const char *comment,
                                const uint8_t *data, uint32_t len) {
    if (!bup_ready) saturn_bup_init();
    // Ensure the device is present and formatted (format only when actually writing).
    BupStat st;
    int32_t s = BUP_Stat((uint32_t) device, 1, &st);
    if (s == BUP_UNFORMAT) { BUP_Format((uint32_t) device); s = BUP_Stat((uint32_t) device, 1, &st); }
    if (s != 0) return 0;
    BupDir dir;
    copy_name(dir.filename, name);
    int i;
    for (i = 0; i < 11; i++) dir.comment[i] = 0;
    for (i = 0; i < 10 && comment[i]; i++) dir.comment[i] = (uint8_t) comment[i];
    dir.language = BUP_ENGLISH;
    dir.date = 0;
    dir.datasize = len;
    dir.blocksize = 0;                // filled by the driver
    s = BUP_Write((uint32_t) device, &dir, (uint8_t *) data, 0 /* overwrite */);
    return (s == 0) ? 1 : 0;
}

extern "C" int saturn_bup_read(int device, const char *name, uint8_t *data) {
    if (!saturn_bup_present(device)) return 0;
    uint8_t fn[12];
    copy_name(fn, name);
    int32_t s = BUP_Read((uint32_t) device, fn, data);
    return (s == 0) ? 1 : 0;
}

extern "C" int saturn_bup_info(int device, const char *name, char *out_comment) {
    out_comment[0] = 0;
    if (!saturn_bup_present(device)) return 0;
    uint8_t fn[12];
    copy_name(fn, name);
    BupDir dir;
    int32_t n = BUP_Dir((uint32_t) device, fn, 1, &dir);   // 1-entry table; n = files found
    if (n <= 0) return 0;
    int i;
    for (i = 0; i < 10; i++) out_comment[i] = (char) dir.comment[i];
    out_comment[10] = 0;
    return 1;
}
