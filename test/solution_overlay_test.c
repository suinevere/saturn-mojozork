/* Regression test: the solution overlay must make walkthrough-only vocabulary
   (e.g. the Lurking Horror password "uhlersoth", which is NOT in the game's
   parser dictionary) suggestible by INSERTING it into the trie, not just boosting
   words that already exist. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "typeahead.h"
#include "typeahead_solution.h"

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void  typeahead_free(void* p) { free(p); }

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

int main(void) {
    int fails = 0;
    TrieNode* root = create_trie_node();
    insert_trie(root, create_word("type", TYPE_VERB, 50));   /* verb before the password */
    insert_trie(root, create_word("open", TYPE_VERB, 50));

    /* Fake Lurking Horror header: release 219 (0x00DB) @0x02, serial "870912" @0x12. */
    unsigned char story[64]; memset(story, 0, sizeof story);
    story[2] = 0x00; story[3] = 0xDB;
    memcpy(story + 0x12, "870912", 6);

    CHECK(find_exact_word(root, "uhlersoth") == NULL);   /* not a dictionary word */

    apply_solution_overlay(root, story, sizeof story);

    CHECK(find_exact_word(root, "uhlersoth") != NULL);   /* overlay must insert it */

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
