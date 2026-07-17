/* The twelve compass/look/inventory/quit/wait abbreviations -- n ne e se s sw w
   nw l i q z -- must always be accepted (a real trie word) AND suggested (shown
   by predict_candidates) at the prompt, in BOTH Easy and Normal difficulty, even
   when the loaded story's own dictionary doesn't define them all (e.g. "q" for
   Quit, which the Saturn client adds itself -- see typeahead_add_abbreviations).
   Easy/Normal only differ in how MID-COMMAND context is filtered; as the first
   word of a command (no prev_word) neither mode filters trie completions, so
   this exercises that path directly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "typeahead.h"

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void  typeahead_free(void* p) { free(p); }

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)
static int fails = 0;

static const char* ABBREVS[] = { "n","ne","e","se","s","sw","w","nw","l","i","q","z" };
#define NABBR (int)(sizeof(ABBREVS) / sizeof(ABBREVS[0]))

static int suggested(TrieNode* root, const char* token) {
    DictionaryWord* out[24];
    int n = predict_candidates(root, NULL, token, out, 24, 1);
    for (int i = 0; i < n; i++) if (!strcmp(out[i]->text, token)) return 1;
    return 0;
}

static void check_all_modes(TrieNode* root, const char* label) {
    for (int i = 0; i < NABBR; i++) {
        const char* tok = ABBREVS[i];
        DictionaryWord* w = find_exact_word(root, tok);
        if (!w) printf("FAIL(%s): \"%s\" not accepted (missing from trie)\n", label, tok);
        CHECK(w != NULL);
        int sug = suggested(root, tok);
        if (!sug) printf("FAIL(%s): \"%s\" not suggested\n", label, tok);
        CHECK(sug);
    }
}

int main(void) {
    TrieNode* root = create_trie_node();

    /* Simulate a partial story dictionary: some ordinary vocabulary, plus one
       abbreviation ("n") the story's own dictionary already defines -- the
       always-accept mechanism must leave an already-present word alone rather
       than duplicate/clobber it. The rest of the twelve are absent, as a v3
       game's real dictionary may leave them out (most notably "q" for Quit). */
    DictionaryWord* north = create_word("north", TYPE_DIRECTION, 48); insert_trie(root, north);
    DictionaryWord* n_dir = create_word("n",     TYPE_DIRECTION, 48); insert_trie(root, n_dir);
    DictionaryWord* take  = create_word("take",  TYPE_VERB, 46);      insert_trie(root, take);
    DictionaryWord* lamp  = create_word("lamp",  TYPE_NOUN, 40);      insert_trie(root, lamp);

    typeahead_add_abbreviations(root);

    /* Pre-existing "n" must not have been duplicated/replaced. */
    CHECK(find_exact_word(root, "n") == n_dir);

    typeahead_set_easy(0, 1);   /* Normal, game has a solution overlay */
    check_all_modes(root, "NORMAL/solution");

    typeahead_set_easy(0, 0);   /* Normal, no solution overlay */
    check_all_modes(root, "NORMAL/no-solution");

    typeahead_set_easy(1, 1);   /* Easy, game has a solution overlay */
    check_all_modes(root, "EASY/solution");

    typeahead_set_easy(1, 0);   /* Easy, no solution overlay (behaves like Normal) */
    check_all_modes(root, "EASY/no-solution");

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
