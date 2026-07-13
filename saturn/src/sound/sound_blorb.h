/*----------------------
 | sound_blorb.h
 | Description: A minimal Blorb (.BLB) sound-index reader: parse the resource index
 |   via a caller-supplied read callback (no platform dependency), look up a
 |   sound's PCM data by Z-machine number, and clear the index. Implemented in
 |   sound_blorb.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef SOUND_BLORB_H
#define SOUND_BLORB_H
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | sound_read_fn
 | Description: The disc read callback: reads `len` bytes at `off` into `buf`;
 |   returns 1 on success, 0 on failure.
 | Author: suinevere
 ----------------------*/
typedef int (*sound_read_fn)(unsigned int off, unsigned int len, unsigned char* buf);

/*----------------------
 | sound_blorb_open
 | Description: Parses a Blorb via `read`. Returns the number of sounds indexed (0
 |   if the file is absent or not a valid Blorb).
 | Author: suinevere
 ----------------------*/
int  sound_blorb_open(sound_read_fn read);

/*----------------------
 | sound_blorb_get
 | Description: Looks up a sound by Z-machine number. On success returns 1 and
 |   fills the SSND data offset + length (bytes within the file), the sample rate
 |   (Hz), and whether it loops. Returns 0 if the number is unknown.
 | Author: suinevere
 ----------------------*/
int  sound_blorb_get(int number, unsigned int* off, unsigned int* len,
                     unsigned short* rate, int* loops);

/*----------------------
 | sound_blorb_close
 | Description: Clears the index (call on game switch).
 | Author: suinevere
 ----------------------*/
void sound_blorb_close(void);

#ifdef __cplusplus
}
#endif
#endif
