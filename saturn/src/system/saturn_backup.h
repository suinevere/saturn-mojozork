/*----------------------
 | saturn_backup.h
 | Description: Save/load the game blob to Saturn backup devices (internal RAM,
 |   cartridge) via the SGL/BIOS backup driver: init, presence check, write, read,
 |   and comment-only info. Implemented in saturn_backup.cxx.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SATURN_BUP_CONSOLE / SATURN_BUP_CARTRIDGE
 | Description: Device numbers for the BIOS backup driver (0-based, matching Jo
 |   Engine): 0 = internal console backup, 1 = cartridge. sega_bup.h's
 |   BUP_MAIN_UNIT(1)/BUP_CURTRIDGE(2) are for the separate SBL library and are off
 |   by one for this driver.
 | Author: suinevere
 ----------------------*/
#define SATURN_BUP_CONSOLE    0
#define SATURN_BUP_CARTRIDGE  1

/*----------------------
 | saturn_bup_init
 | Description: Initializes the SGL/BIOS backup driver. Call once at boot.
 | Author: suinevere
 ----------------------*/
void saturn_bup_init(void);

/*----------------------
 | saturn_bup_present
 | Description: 1 if the device is present and usable (formatting it first if
 |   present but unformatted); used to hide the cartridge option when absent.
 | Author: suinevere
 ----------------------*/
int  saturn_bup_present(int device);

/*----------------------
 | saturn_bup_write / saturn_bup_read / saturn_bup_info
 | Description: write stores len bytes under name+comment (overwriting a same-named
 |   file); read loads a named save into `data` (sized for the stored blob); info
 |   copies a save's comment (<=10 chars, NUL-terminated) into out_comment. Each
 |   returns 1 on success, 0 otherwise.
 | Author: suinevere
 ----------------------*/
int  saturn_bup_write(int device, const char *name, const char *comment,
                      const uint8_t *data, uint32_t len);
int  saturn_bup_read(int device, const char *name, uint8_t *data);
int  saturn_bup_info(int device, const char *name, char *out_comment);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BACKUP_H */
