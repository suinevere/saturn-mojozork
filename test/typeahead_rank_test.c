/* Reproduce the "TURN PC " ranking: walkthrough transitions (pc->on/off) vs an
   on-screen noun ("exit" visible on the PC screen). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input/typeahead.h"

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void  typeahead_free(void* p) { free(p); }

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)
static int fails = 0;

static void show(const char* label, TrieNode* root, DictionaryWord* prev,
                 const char* want0, const char* want1) {
    DictionaryWord* out[24];
    int n = predict_candidates(root, prev, "", out, 24, 0);
    printf("%s -> ", label);
    for (int i = 0; i < n; i++) printf("%s ", out[i]->text);
    printf("\n");
    CHECK(n >= 2);
    /* the two walkthrough continuations must lead, before the on-screen "exit" */
    if (n >= 2) {
        int lead0 = !strcmp(out[0]->text, want0) || !strcmp(out[0]->text, want1);
        int lead1 = !strcmp(out[1]->text, want0) || !strcmp(out[1]->text, want1);
        CHECK(lead0 && lead1);
    }
}

int main(void) {
    TrieNode* root = create_trie_node();
    DictionaryWord* turn = create_word("turn", TYPE_VERB, 50);  insert_trie(root, turn);
    DictionaryWord* pc   = create_word("pc",   TYPE_NOUN, 40);  insert_trie(root, pc);
    DictionaryWord* on   = create_word("on",   TYPE_PREP, 40);  insert_trie(root, on);
    DictionaryWord* off  = create_word("off",  TYPE_PREP, 40);  insert_trie(root, off);
    DictionaryWord* ex   = create_word("exit", TYPE_NOUN, 40);  insert_trie(root, ex);
    DictionaryWord* li   = create_word("light",TYPE_NOUN, 40);  insert_trie(root, li);
    DictionaryWord* fk   = create_word("forklift",TYPE_NOUN,40);insert_trie(root, fk);

    add_solution_link(pc, on, 58);   add_solution_link(pc, off, 58);
    add_solution_link(turn, pc, 66); add_solution_link(turn, li, 58); add_solution_link(turn, fk, 58);

    /* The PC screen shows an "exit" option -> it becomes on-screen "hot". */
    typeahead_set_screen(root, "exit edit read");

    show("turn pc ", root, pc, "on", "off");
    show("turn ",    root, turn, "pc", "light");   /* pc/light/forklift lead; check top 2 */

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
