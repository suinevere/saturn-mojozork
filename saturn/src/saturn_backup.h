#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device numbers for the BIOS backup driver (0-based, matching Jo Engine which
   drives the same BIOS routines): 0 = internal console backup, 1 = cartridge.
   sega_bup.h's BUP_MAIN_UNIT(1)/BUP_CURTRIDGE(2) are for the separate SBL library
   and are off by one for this driver. */
#define SATURN_BUP_CONSOLE    0
#define SATURN_BUP_CARTRIDGE  1

/* Initialize the SGL/BIOS backup driver. Call once at boot. */
void saturn_bup_init(void);

/* 1 if the device is present and usable (formatting it first if it is present
   but unformatted). Used to hide the cartridge option when absent. */
int  saturn_bup_present(int device);

/* Write len bytes under filename+comment (overwriting an existing file of the
   same name). Returns 1 on success. */
int  saturn_bup_write(int device, const char *name, const char *comment,
                      const uint8_t *data, uint32_t len);

/* Read a save named `name` into `data` (must be large enough for the stored
   size). Returns 1 on success. */
int  saturn_bup_read(int device, const char *name, uint8_t *data);

/* If a save named `name` exists, copy its comment (<=10 chars, NUL-terminated)
   into out_comment and return 1; otherwise return 0. */
int  saturn_bup_info(int device, const char *name, char *out_comment);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BACKUP_H */
