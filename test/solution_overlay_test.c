/* Regression test: the solution overlay must make walkthrough-only vocabulary
   (e.g. the Lurking Horror password "uhlersoth", which is NOT in the game's
   parser dictionary) suggestible by INSERTING it into the trie, not just boosting
   words that already exist. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input/typeahead.h"
#include "input/typeahead_solution.h"

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

    /* Walkthrough transitions must be applied to the trie: "TURN PC ON/OFF" ->
       pc links to on and off; "TURN {PC,LIGHT,FORKLIFT}" -> turn links to those. */
    DictionaryWord* pc = find_exact_word(root, "pc");
    CHECK(pc != NULL);
    int has_on = 0, has_off = 0, pc_n = 0;
    if (pc) for (NextWordLink* l = pc->next_words; l; l = l->next) {
        pc_n++;
        if (!strcmp(l->target_word->text, "on"))  has_on = 1;
        if (!strcmp(l->target_word->text, "off")) has_off = 1;
    }
    printf("pc: %d next_words, on=%d off=%d\n", pc_n, has_on, has_off);
    CHECK(has_on); CHECK(has_off);

    DictionaryWord* turn = find_exact_word(root, "turn");
    CHECK(turn != NULL);
    int t_pc = 0, t_light = 0, t_fork = 0, turn_n = 0;
    if (turn) for (NextWordLink* l = turn->next_words; l; l = l->next) {
        turn_n++;
        if (!strcmp(l->target_word->text, "pc"))       t_pc = 1;
        if (!strcmp(l->target_word->text, "light"))    t_light = 1;
        if (!strcmp(l->target_word->text, "forklift")) t_fork = 1;
    }
    printf("turn: %d next_words, pc=%d light=%d forklift=%d\n", turn_n, t_pc, t_light, t_fork);
    CHECK(t_pc); CHECK(t_light); CHECK(t_fork);

    /* End-to-end ranking with the REAL overlay: an on-screen distractor ("exit")
       must NOT out-rank the walkthrough path, and the path must surface in
       walkthrough order. */
    insert_trie(root, create_word("exit", TYPE_NOUN, 40));
    typeahead_set_screen(root, "exit");
    DictionaryWord* out[24];
    int n = predict_candidates(root, pc, "", out, 24, 0);
    printf("turn pc -> "); for (int i = 0; i < n; i++) printf("%s ", out[i]->text); printf("\n");
    CHECK(n >= 2 && !strcmp(out[0]->text, "on") && !strcmp(out[1]->text, "off"));
    n = predict_candidates(root, turn, "", out, 24, 0);
    printf("turn -> "); for (int i = 0; i < n; i++) printf("%s ", out[i]->text); printf("\n");
    CHECK(n >= 3 && !strcmp(out[0]->text, "pc") && !strcmp(out[1]->text, "light")
                 && !strcmp(out[2]->text, "forklift"));

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
