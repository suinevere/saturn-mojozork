/*----------------------
 | saturn_backup.cxx
 | Description: A thin wrapper over the SGL BUP (backup) library for saving and
 |   loading the game blob to the internal RAM, cartridge, or other backup
 |   devices. Handles one-time driver init, presence checks that never format
 |   (so a probe cannot wipe a cartridge), and formatting only at write time.
 | Author: suinevere
 | Dependencies: SRL, sega_bup.h (the SGL BUP API), saturn_backup.h
 ----------------------*/
#include <srl.hpp>
#include <sega_bup.h>
#include "saturn_backup.h"

/*----------------------
 | bup_lib / bup_work / bup_cfg / bup_ready
 | Description: The BUP driver workspaces (16 KB library + 8 KB work, sizes per Jo
 |   Engine's backup init), the per-device config table, and a one-time init flag.
 | Author: suinevere
 ----------------------*/
static uint32_t  bup_lib[16384 / 4];
static uint32_t  bup_work[8192 / 4];
static BupConfig bup_cfg[3];
static int       bup_ready = 0;

/*----------------------
 | copy_name
 | Description: Copies a filename into the BUP driver's fixed 12-byte field,
 |   zero-padded and truncated to 11 chars.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- the 12-byte destination; name -- the source filename
 | Returns: N/A
 ----------------------*/
static void copy_name(uint8_t out[12], const char *name) {
    int i;
    for (i = 0; i < 12; i++) out[i] = 0;
    for (i = 0; i < 11 && name[i]; i++) out[i] = (uint8_t) name[i];
}

/*----------------------
 | saturn_bup_init
 | Description: Initializes the BUP driver once; subsequent calls are no-ops.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_lib, bup_work, bup_cfg, bup_ready
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_bup_init(void) {
    if (bup_ready) return;
    BUP_Init((uint32_t *) bup_lib, bup_work, bup_cfg);
    bup_ready = 1;
}

/*----------------------
 | saturn_bup_present
 | Description: True when a device answers BUP_Stat (whether formatted or not);
 |   absent devices return BUP_NON/error. Never formats, so a mere presence check
 |   cannot wipe a cartridge.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_ready
 | Params: device -- the backup device id
 | Returns: 1 if present, 0 otherwise
 ----------------------*/
extern "C" int saturn_bup_present(int device) {
    if (!bup_ready) saturn_bup_init();
    BupStat st;
    int32_t s = BUP_Stat((uint32_t) device, 1, &st);
    return (s == 0 || s == BUP_UNFORMAT) ? 1 : 0;
}

/*----------------------
 | saturn_bup_write
 | Description: Writes a named save to a device, formatting it first only if it is
 |   present but unformatted (so formatting happens at write time, never on a
 |   probe). Fills a BupDir with the name, comment, English language tag, and data
 |   size, and overwrites any existing file of the same name.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: bup_ready
 | Params: device -- backup device id; name -- save filename; comment -- display
 |   comment; data -- blob bytes; len -- blob length
 | Returns: 1 on success, 0 on failure
 ----------------------*/
extern "C" int saturn_bup_write(int device, const char *name, const char *comment,
                                const uint8_t *data, uint32_t len) {
    if (!bup_ready) saturn_bup_init();
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
    dir.blocksize = 0;
    s = BUP_Write((uint32_t) device, &dir, (uint8_t *) data, 0);
    return (s == 0) ? 1 : 0;
}

/*----------------------
 | saturn_bup_read
 | Description: Reads a named save off a present device into `data` (the caller
 |   sizes the buffer to the record).
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: N/A
 | Params: device -- backup device id; name -- save filename; data -- destination
 | Returns: 1 on success, 0 if absent or unreadable
 ----------------------*/
extern "C" int saturn_bup_read(int device, const char *name, uint8_t *data) {
    if (!saturn_bup_present(device)) return 0;
    uint8_t fn[12];
    copy_name(fn, name);
    int32_t s = BUP_Read((uint32_t) device, fn, data);
    return (s == 0) ? 1 : 0;
}

/*----------------------
 | saturn_bup_info
 | Description: Reads a save's display comment (into a 10-char + NUL buffer)
 |   without loading its data, so the slot picker can show what a slot holds.
 | Author: suinevere
 | Dependencies: sega_bup.h
 | Globals: N/A
 | Params: device -- backup device id; name -- save filename; out_comment --
 |   receives the comment (blanked when absent)
 | Returns: 1 if the file exists, 0 otherwise
 ----------------------*/
extern "C" int saturn_bup_info(int device, const char *name, char *out_comment) {
    out_comment[0] = 0;
    if (!saturn_bup_present(device)) return 0;
    uint8_t fn[12];
    copy_name(fn, name);
    BupDir dir;
    int32_t n = BUP_Dir((uint32_t) device, fn, 1, &dir);
    if (n <= 0) return 0;
    int i;
    for (i = 0; i < 10; i++) out_comment[i] = (char) dir.comment[i];
    out_comment[10] = 0;
    return 1;
}
