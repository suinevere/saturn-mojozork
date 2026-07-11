#ifndef SOUND_BLORB_H
#define SOUND_BLORB_H
#ifdef __cplusplus
extern "C" {
#endif

/* Reads `len` bytes at `off` into `buf`; returns 1 on success, 0 on failure. */
typedef int (*sound_read_fn)(unsigned int off, unsigned int len, unsigned char* buf);

/* Parse a Blorb (.BLB) via `read`. Returns the number of sounds indexed
   (0 if the file is absent or not a valid Blorb). */
int  sound_blorb_open(sound_read_fn read);

/* Look up a sound by its Z-machine number. On success returns 1 and fills the
   SSND data offset + length (bytes within the file), the sample rate (Hz), and
   whether it loops. Returns 0 if the number is unknown. */
int  sound_blorb_get(int number, unsigned int* off, unsigned int* len,
                     unsigned short* rate, int* loops);

/* Clear the index (call on game switch). */
void sound_blorb_close(void);

#ifdef __cplusplus
}
#endif
#endif
