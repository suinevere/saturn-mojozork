#ifndef SATURN_GLUE_H
#define SATURN_GLUE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hooks implemented in main.cxx (C++), called by the Z-Machine core (C).
   Signatures MUST match the ZMachineState function pointers:
     writestr: void(*)(const char*, size_t)   [uintptr == size_t]
     readline: void(*)(char*, int)
     die:      void(*)(const char*, ...)                                    */
void saturn_writestr(const char *str, size_t slen);
void saturn_readline(char *buf, int maxlen);
#if defined(__GNUC__) || defined(__clang__)
void saturn_die(const char *fmt, ...) __attribute__((noreturn));
#else
void saturn_die(const char *fmt, ...);
#endif

/* Re-read ZORK1.Z3 from CD into buf (len bytes). Returns 1 on success, 0 on failure.
   Used by opcode_restart on Saturn (there is no fopen). */
int saturn_read_story_file(uint8_t *buf, uint32_t len);

/* Save/restore the Z-machine state to Saturn backup memory (these present the
   on-screen device/slot menu). Return 1 on success, 0 on cancel-or-fail.
   Implemented in main.cxx. */
int saturn_save_blob(const uint8_t *data, uint32_t len);
int saturn_load_blob(uint8_t *buf, uint32_t maxlen);

/* Entry points implemented in mojozork_saturn.c, called by main.cxx. */
void mojo_boot(uint8_t *story, uint32_t len, int seed);
void mojo_run(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_GLUE_H */
