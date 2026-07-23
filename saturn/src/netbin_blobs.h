/*----------------------
 | netbin_blobs.h
 | Description: Accessors for the payloads embedded in the .netbin build --
 |   the Zork I story image. The CD build compiles the same translation unit
 |   but links no data: every accessor returns NULL/0 there, so callers branch
 |   on the size rather than on a build flag.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/

#ifndef NETBIN_BLOBS_H
#define NETBIN_BLOBS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Zork I story image. NULL/0 in the CD build. */
const unsigned char *netbin_story_data(void);
unsigned int         netbin_story_size(void);

#ifdef __cplusplus
}
#endif

#endif /* NETBIN_BLOBS_H */
