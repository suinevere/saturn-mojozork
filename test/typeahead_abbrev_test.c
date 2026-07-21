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

/* The solution overlay inserts walkthrough vocabulary the story's dictionary lacks
   as TYPE_UNKNOWN (typeahead_solution.c). Because the extractor drops the bare
   direction abbreviations, a walkthrough that says "s" lands exactly there -- so by
   the time the abbreviations are added, "s" is already present but UNCLASSIFIED.
   That matters at scale: cand_collect only gives the +2000 first-word bonus to a
   VERB/DIRECTION, so an UNKNOWN "s" sits at its raw weight and is the first thing
   evicted once CAND_MAX candidates share the prefix -- accepted, but never
   suggested. A 4-word trie can't show this; a real dictionary has dozens of
   s-verbs. Reproduce that shape. */
static void check_overlay_stub_is_reclassified(void) {
    TrieNode* root = create_trie_node();
    static const char* SVERBS[] = {
        "say","search","shout","show","sing","sit","skip","sleep","slide","smell",
        "snap","spin","spray","squeeze","stab","stand","start","stay","steal","step",
        "stop","strike","swim","swing","switch","save","score","script","send","set",
        "shake","sharpen","shave","shed","shoot","shut","sign","slap","slice","smash"
    };
    int nv = (int)(sizeof(SVERBS) / sizeof(SVERBS[0]));
    for (int i = 0; i < nv; i++)
        insert_trie(root, create_word(SVERBS[i], TYPE_VERB, 46));
    /* The overlay got here first: "s" exists, but only as an unclassified stub. */
    insert_trie(root, create_word("s", TYPE_UNKNOWN, 41));

    typeahead_add_abbreviations(root);

    DictionaryWord* s = find_exact_word(root, "s");
    CHECK(s != NULL);
    if (s && s->type != TYPE_DIRECTION) printf("FAIL: overlay's \"s\" left as type %d, want TYPE_DIRECTION (%d)\n",
                                               (int) s->type, (int) TYPE_DIRECTION);
    CHECK(s && s->type == TYPE_DIRECTION);
    typeahead_set_easy(0, 1);
    if (!suggested(root, "s")) printf("FAIL: \"s\" evicted from suggestions among %d s-verbs\n", nv);
    CHECK(suggested(root, "s"));
    destroy_typeahead(root);
}

/* d(own), u(p), g (again) and x (examine) are whole commands the trie does not
   hold as words of their own, so nothing can float them to the front of their own
   completions the way the twelve above are floated. Typed as the first word they
   must offer NOTHING, so Accept submits the bare letter instead of growing it into
   "door"/"under"/"get"/"xyzzy". Mid-command they are an ordinary prefix again. */
static void check_single_letter_pass_through(void) {
    TrieNode* root = create_trie_node();
    insert_trie(root, create_word("door",  TYPE_NOUN, 40));
    insert_trie(root, create_word("under", TYPE_VERB, 46));
    insert_trie(root, create_word("get",   TYPE_VERB, 46));
    insert_trie(root, create_word("xyzzy", TYPE_VERB, 46));
    typeahead_add_abbreviations(root);
    typeahead_set_easy(0, 0);

    DictionaryWord* out[24];
    static const char* PASS[] = { "d", "u", "g", "x" };
    for (int i = 0; i < 4; i++) {
        int n = predict_candidates(root, NULL, PASS[i], out, 24, 1);
        if (n != 0) printf("FAIL: \"%s\" offered %d suggestion(s), first \"%s\"\n",
                           PASS[i], n, out[0]->text);
        CHECK(n == 0);
    }
    /* Two letters is a real prefix again, and so is a "d" past the first word. */
    CHECK(predict_candidates(root, NULL, "do", out, 24, 1) > 0);
    CHECK(predict_candidates(root, NULL, "d", out, 24, 0) > 0);
    destroy_typeahead(root);
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

    check_overlay_stub_is_reclassified();
    check_single_letter_pass_through();

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
