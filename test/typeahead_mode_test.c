/* Easy vs Normal prediction modes:
   - Normal: full grammar, but drop invalid command shapes (verb->verb,
     noun->noun, verb + a noun that isn't its object) unless a solution link.
   - Easy: restrict context to the winning path (solution links) when the game
     has a solution; otherwise behave like Normal. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "typeahead.h"

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void  typeahead_free(void* p) { free(p); }

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)
static int fails = 0;

static int has(DictionaryWord** out, int n, const char* w) {
    for (int i = 0; i < n; i++) if (!strcmp(out[i]->text, w)) return 1;
    return 0;
}
static void dump(const char* label, TrieNode* root, DictionaryWord* prev) {
    DictionaryWord* out[24];
    int n = predict_candidates(root, prev, "", out, 24, 0);
    printf("%s -> ", label);
    for (int i = 0; i < n; i++) printf("%s ", out[i]->text);
    printf("\n");
}

int main(void) {
    TrieNode* root = create_trie_node();
    DictionaryWord* turn = create_word("turn",  TYPE_VERB, 50); insert_trie(root, turn);
    DictionaryWord* take = create_word("take",  TYPE_VERB, 50); insert_trie(root, take);
    DictionaryWord* pc   = create_word("pc",    TYPE_NOUN, 40); insert_trie(root, pc);
    DictionaryWord* dial = create_word("dial",  TYPE_NOUN, 40); insert_trie(root, dial);
    DictionaryWord* exitw= create_word("exit",  TYPE_NOUN, 40); insert_trie(root, exitw);
    DictionaryWord* signs= create_word("signs", TYPE_NOUN, 40); insert_trie(root, signs);
    DictionaryWord* on   = create_word("on",    TYPE_PREP, 40); insert_trie(root, on);
    DictionaryWord* off  = create_word("off",   TYPE_PREP, 40); insert_trie(root, off);

    add_next_word(turn, dial, 60);            // grammar object of turn
    add_solution_link(turn, pc, 100);         // winning path
    add_solution_link(pc, on, 100);
    add_solution_link(pc, off, 90);

    typeahead_set_screen(root, "exit signs"); // both visible on screen

    DictionaryWord* out[24]; int n;

    /* ---- Normal (game has a solution) ---- */
    typeahead_set_easy(0, 1);
    dump("NORMAL turn",   root, turn);
    dump("NORMAL turn pc", root, pc);
    n = predict_candidates(root, turn, "", out, 24, 0);
    CHECK(has(out, n, "pc") && has(out, n, "dial"));   // solution + grammar object
    CHECK(!has(out, n, "exit"));                        // verb + non-object noun -> dropped
    CHECK(!has(out, n, "take"));                        // verb -> verb -> dropped
    n = predict_candidates(root, pc, "", out, 24, 0);
    CHECK(has(out, n, "on") && has(out, n, "off"));
    CHECK(!has(out, n, "signs") && !has(out, n, "exit"));  // noun -> noun -> dropped

    /* ---- Easy (game has a solution): winning path ONLY ---- */
    typeahead_set_easy(1, 1);
    dump("EASY turn pc", root, pc);
    n = predict_candidates(root, pc, "", out, 24, 0);
    CHECK(n == 2 && has(out, n, "on") && has(out, n, "off"));   // ONLY on/off
    n = predict_candidates(root, turn, "", out, 24, 0);
    CHECK(has(out, n, "pc") && !has(out, n, "dial"));           // solution only, no grammar

    /* ---- Easy but NO solution file: behaves like Normal ---- */
    typeahead_set_easy(1, 0);
    n = predict_candidates(root, pc, "", out, 24, 0);
    CHECK(has(out, n, "on") && has(out, n, "off"));             // like normal (solution links present)
    CHECK(!has(out, n, "signs"));

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
