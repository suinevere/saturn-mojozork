/* Verifies gen_blob.py round-trips bytes exactly, and that the non-NETBIN
   build links empty accessors. Built twice by the runner: once with -DNETBIN
   against a generated fixture, once without. */
#include "../src/netbin_blobs.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
#ifdef NETBIN
    /* fixture_blobs.c is generated from a known 4-byte pattern file. */
    assert(netbin_story_size() == 4);
    assert(netbin_story_data() != 0);
    assert(netbin_story_data()[0] == 0x00);
    assert(netbin_story_data()[1] == 0x7f);
    assert(netbin_story_data()[2] == 0x80);
    assert(netbin_story_data()[3] == 0xff);
    printf("test_netbin_blobs (NETBIN): OK\n");
#else
    assert(netbin_story_size() == 0);
    assert(netbin_story_data() == 0);
    printf("test_netbin_blobs (CD): OK\n");
#endif
    return 0;
}
