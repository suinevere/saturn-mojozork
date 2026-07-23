/*----------------------
 | typeahead.c
 | Description: The typeahead prediction core: a letter trie of DictionaryWords
 |   with directional transition links, and the ranking that turns a prefix (plus
 |   the previous word and the on-screen vocabulary) into an ordered candidate
 |   list. Holds the compass ordering, the Easy/Normal prediction modes, the
 |   on-screen "hot" word marking, and the stock command abbreviations. Pure logic
 |   over TYPEAHEAD_MALLOC; the story-driven build lives in typeahead_extract.c and
 |   the walkthrough boosts in typeahead_solution.c.
 | Author: suinevere
 | Dependencies: typeahead.h (the trie/word types + TYPEAHEAD_MALLOC/FREE),
 |   stdlib.h, string.h, ctype.h
 ----------------------*/
#include "typeahead.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*----------------------
 | typeahead_strdup
 | Description: Duplicates a string into a TYPEAHEAD_MALLOC allocation.
 | Author: suinevere
 ----------------------*/
static char* typeahead_strdup(const char* s) {
    if (!s) return NULL;
    int len = strlen(s);
    char* copy = (char*)TYPEAHEAD_MALLOC(len + 1);
    if (copy) {
        strcpy(copy, s);
    }
    return copy;
}

/*----------------------
 | create_word
 | Description: Allocates a DictionaryWord with its own text copy, part-of-speech
 |   type, and base weight, and no transition links yet.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: N/A
 | Params: text -- the word text; type -- WordType; weight -- base weight
 | Returns: the new word
 ----------------------*/
DictionaryWord* create_word(const char* text, WordType type, int weight) {
    DictionaryWord* word = (DictionaryWord*)TYPEAHEAD_MALLOC(sizeof(DictionaryWord));
    word->text = typeahead_strdup(text);
    word->type = type;
    word->base_weight = weight;
    word->next_words = NULL;
    word->hot_gen = 0;
    return word;
}

/*----------------------
 | add_next_word
 | Description: Records a directional association (source is likely followed by
 |   target) as a weighted link prepended to source's list; not a solution link.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: N/A
 | Params: source, target -- the two words; weight -- the transition weight
 | Returns: N/A
 ----------------------*/
void add_next_word(DictionaryWord* source, DictionaryWord* target, int weight) {
    NextWordLink* link = (NextWordLink*)TYPEAHEAD_MALLOC(sizeof(NextWordLink));
    link->target_word = target;
    link->transition_weight = weight;
    link->solution = 0;
    link->next = source->next_words;
    source->next_words = link;
}

/*----------------------
 | add_solution_link
 | Description: Like add_next_word but marks the new (just-prepended) link as a
 |   winning-path solution-overlay link.
 | Author: suinevere
 ----------------------*/
void add_solution_link(DictionaryWord* source, DictionaryWord* target, int weight) {
    add_next_word(source, target, weight);
    source->next_words->solution = 1;
}

/*----------------------
 | create_trie_node
 | Description: Allocates an empty trie node (no letter, children, word, or
 |   best-completion cache).
 | Author: suinevere
 ----------------------*/
TrieNode* create_trie_node() {
    TrieNode* node = (TrieNode*)TYPEAHEAD_MALLOC(sizeof(TrieNode));
    node->first_child = NULL;
    node->next_sibling = NULL;
    node->letter = 0;
    node->word_data = NULL;
    node->best_completion = NULL;
    return node;
}

/*----------------------
 | find_child
 | Description: Returns the child of `node` for lowercase letter `c`, or NULL,
 |   walking the sibling list (short for English tries).
 | Author: suinevere
 ----------------------*/
static TrieNode* find_child(TrieNode* node, char c) {
    for (TrieNode* ch = node->first_child; ch != NULL; ch = ch->next_sibling) {
        if (ch->letter == c) return ch;
    }
    return NULL;
}

/*----------------------
 | insert_trie
 | Description: Inserts a word into the trie letter by letter (skipping
 |   non-letters), creating nodes as needed, and updates each traversed node's
 |   best_completion cache to the heaviest word passing through it. Marks the
 |   final node with the word.
 | Author: suinevere
 | Dependencies: typeahead.h, string.h, ctype.h
 | Globals: N/A
 | Params: root -- the trie root; word -- the word to insert
 | Returns: N/A
 ----------------------*/
void insert_trie(TrieNode* root, DictionaryWord* word) {
    TrieNode* current = root;
    int len = strlen(word->text);

    for (int i = 0; i < len; i++) {
        char c = (char)tolower((unsigned char)word->text[i]);
        if (c < 'a' || c > 'z') continue; // Skip non-letters

        TrieNode* child = find_child(current, c);
        if (child == NULL) {
            child = create_trie_node();
            child->letter = c;
            child->next_sibling = current->first_child; // prepend to child list
            current->first_child = child;
        }
        current = child;

        // Update the best-completion cache as we traverse down.
        if (current->best_completion == NULL ||
            word->base_weight > current->best_completion->base_weight) {
            current->best_completion = word;
        }
    }
    current->word_data = word;
}

/*----------------------
 | destroy_typeahead
 | Description: Recursively frees the trie plus each word's links and text. A
 |   DictionaryWord is the word_data of exactly one node, so every allocation is
 |   freed once.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: N/A
 | Params: node -- the subtree root to free
 | Returns: N/A
 ----------------------*/
void destroy_typeahead(TrieNode* node) {
    if (node == NULL) return;
    TrieNode* c = node->first_child;
    while (c != NULL) { TrieNode* next = c->next_sibling; destroy_typeahead(c); c = next; }
    if (node->word_data != NULL) {
        NextWordLink* l = node->word_data->next_words;
        while (l != NULL) { NextWordLink* n = l->next; TYPEAHEAD_FREE(l); l = n; }
        TYPEAHEAD_FREE(node->word_data->text);
        TYPEAHEAD_FREE(node->word_data);
    }
    TYPEAHEAD_FREE(node);
}

// ---- ranked candidate list (for cycling through suggestions) ----------------

/*----------------------
 | CAND_MAX / FIRST_WORD_POS_BONUS / SCREEN_BONUS / HOT_MAX / EXACT_MATCH_WEIGHT
 | Description: Ranking constants. CAND_MAX caps the candidate arrays.
 |   FIRST_WORD_POS_BONUS lifts verbs/directions at the start of a command (above
 |   base weights, below a context match's 10000). SCREEN_BONUS tops a word's tier
 |   when it is visible on screen without crossing tiers; HOT_MAX caps the
 |   on-screen list. EXACT_MATCH_WEIGHT floats the word the player actually typed
 |   above every other tier so a complete word leads its own completions.
 | Author: suinevere
 ----------------------*/
#define CAND_MAX 32
#define FIRST_WORD_POS_BONUS 2000
#define SCREEN_BONUS 500
#define HOT_MAX 64
#define EXACT_MATCH_WEIGHT 100000

/*----------------------
 | g_hot_gen / g_hot / g_nhot
 | Description: The on-screen ("hot") word set for the current prompt: a generation
 |   counter bumped each time the screen is set (so the previous marks expire for
 |   free), the array of on-screen words for empty-prefix surfacing, and its count.
 | Author: suinevere
 ----------------------*/
static int g_hot_gen = 0;
static DictionaryWord* g_hot[HOT_MAX];
static int g_nhot = 0;

/*----------------------
 | word_hot
 | Description: SCREEN_BONUS if the word is marked for the current on-screen
 |   generation, else 0.
 | Author: suinevere
 ----------------------*/
static int word_hot(DictionaryWord* w) {
    return (g_hot_gen != 0 && w->hot_gen == g_hot_gen) ? SCREEN_BONUS : 0;
}

/*----------------------
 | dir_rank
 | Description: Ranks a direction word for clockwise-compass ordering (north, ne,
 |   east, ... nw, then up, down, in, out); higher sorts earlier, and the full
 |   spelling outranks its abbreviation (north above n). Covers v3 dictionary forms
 |   including 6-char truncations (northe = northeast). 0 if not a direction.
 | Author: suinevere
 | Dependencies: string.h
 | Globals: N/A
 | Params: t -- the word text
 | Returns: a rank (higher = earlier), or 0
 ----------------------*/
static int dir_rank(const char* t) {
    int cv, pref;
    if      (!strcmp(t, "north"))  { cv = 12; pref = 1; }
    else if (!strcmp(t, "n"))      { cv = 12; pref = 0; }
    else if (!strcmp(t, "northe") || !strcmp(t, "northeast")) { cv = 11; pref = 1; }
    else if (!strcmp(t, "ne"))     { cv = 11; pref = 0; }
    else if (!strcmp(t, "east"))   { cv = 10; pref = 1; }
    else if (!strcmp(t, "e"))      { cv = 10; pref = 0; }
    else if (!strcmp(t, "southe") || !strcmp(t, "southeast")) { cv = 9; pref = 1; }
    else if (!strcmp(t, "se"))     { cv = 9;  pref = 0; }
    else if (!strcmp(t, "south"))  { cv = 8;  pref = 1; }
    else if (!strcmp(t, "s"))      { cv = 8;  pref = 0; }
    else if (!strcmp(t, "southw") || !strcmp(t, "southwest")) { cv = 7; pref = 1; }
    else if (!strcmp(t, "sw"))     { cv = 7;  pref = 0; }
    else if (!strcmp(t, "west"))   { cv = 6;  pref = 1; }
    else if (!strcmp(t, "w"))      { cv = 6;  pref = 0; }
    else if (!strcmp(t, "northw") || !strcmp(t, "northwest")) { cv = 5; pref = 1; }
    else if (!strcmp(t, "nw"))     { cv = 5;  pref = 0; }
    else if (!strcmp(t, "up"))     { cv = 4;  pref = 1; }
    else if (!strcmp(t, "u"))      { cv = 4;  pref = 0; }
    else if (!strcmp(t, "down"))   { cv = 3;  pref = 1; }
    else if (!strcmp(t, "d"))      { cv = 3;  pref = 0; }
    else if (!strcmp(t, "in") || !strcmp(t, "inside") || !strcmp(t, "into")) { cv = 2; pref = 1; }
    else if (!strcmp(t, "out"))    { cv = 1;  pref = 1; }
    else return 0;
    return cv * 2 + pref;
}

/*----------------------
 | is_compass / is_diagonal
 | Description: is_compass is true for a cardinal/vertical exit (N/S/E/W/diagonals/
 |   up/down) that should lead the verbs at the first word; in/out and oddballs are
 |   excluded so a lone "o" still offers "open". is_diagonal is true for the four
 |   diagonal directions in their abbreviation, 6-char, and full spellings.
 | Author: suinevere
 ----------------------*/
static int is_compass(const char* t) {
    return !strcmp(t, "north") || !strcmp(t, "south") || !strcmp(t, "east") || !strcmp(t, "west")
        || !strcmp(t, "northeast") || !strcmp(t, "northwest")
        || !strcmp(t, "southeast") || !strcmp(t, "southwest")
        || !strcmp(t, "up") || !strcmp(t, "down");
}

static int is_diagonal(const char* t) {
    return !strcmp(t, "ne") || !strcmp(t, "nw") || !strcmp(t, "se") || !strcmp(t, "sw")
        || !strcmp(t, "northe") || !strcmp(t, "northw") || !strcmp(t, "southe") || !strcmp(t, "southw")
        || !strcmp(t, "northeast") || !strcmp(t, "northwest")
        || !strcmp(t, "southeast") || !strcmp(t, "southwest");
}

/*----------------------
 | DIR_BASE / MIDCMD_VERB_WEIGHT
 | Description: DIR_BASE keeps a direction's compass weight in the verb tier so
 |   common verbs still lead their prefix ("o" -> open, not out).
 |   MIDCMD_VERB_WEIGHT is the weight of a verb suggested mid-command (below
 |   prepositions/nouns/directions, but present so the player can cycle to it --
 |   two verbs in a row is almost never valid).
 | Author: suinevere
 ----------------------*/
#define DIR_BASE 40
#define MIDCMD_VERB_WEIGHT 5

/*----------------------
 | cand_add
 | Description: Adds `w` at `weight`, de-duplicating by pointer (keeping the higher
 |   weight). When the arrays are full it keeps the top CAND_MAX by weight, evicting
 |   the current minimum if this candidate is heavier, so a busy prefix never drops
 |   the best word just because it was reached late.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: cand/wt -- the candidate + weight arrays; n -- current count; w -- word;
 |   weight -- its weight
 | Returns: the new count
 ----------------------*/
static int cand_add(DictionaryWord** cand, int* wt, int n, DictionaryWord* w, int weight) {
    for (int i = 0; i < n; i++)
        if (cand[i] == w) { if (weight > wt[i]) wt[i] = weight; return n; }
    if (n < CAND_MAX) { cand[n] = w; wt[n] = weight; return n + 1; }
    int mi = 0;
    for (int i = 1; i < n; i++) if (wt[i] < wt[mi]) mi = i;
    if (weight > wt[mi]) { cand[mi] = w; wt[mi] = weight; }
    return n;
}

/*----------------------
 | cand_collect
 | Description: Depth-first collects every word in a subtree into the candidate
 |   arrays, weighting each: directions get clockwise-compass order (with first-word
 |   bumps so a lone "s" means south and exits lead the verbs); nouns get their base
 |   plus the on-screen bonus (lifted into the context tier mid-command); verbs get
 |   the first-word position bonus, or drop to MIDCMD_VERB_WEIGHT after the first
 |   word.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: node -- subtree root; cand/wt -- output arrays; n -- count (in/out);
 |   fw -- true at the first word of a command
 | Returns: N/A
 ----------------------*/
static void cand_collect(TrieNode* node, DictionaryWord** cand, int* wt, int* n, int fw) {
    if (node->word_data) {
        DictionaryWord* w = node->word_data;
        int weight;
        if (w->type == TYPE_DIRECTION) {
            weight = DIR_BASE + dir_rank(w->text);       // clockwise compass order
            if (fw && !is_diagonal(w->text)) weight += 3;   // a lone "s" means south, not SE
            if (fw && is_compass(w->text)) weight += 80;    // exits lead the verbs (in/out don't)
        }
        else {
            weight = w->base_weight;
            if (w->type == TYPE_NOUN) {
                int hot = word_hot(w);
                weight += hot;
                // An on-screen object mid-command leads even a grammar-listed
                // off-screen object: lift it into the context tier.
                if (!fw && hot) weight += 10000;
            }
        }
        if (fw && (w->type == TYPE_VERB || w->type == TYPE_DIRECTION))
            weight += FIRST_WORD_POS_BONUS;
        else if (!fw && w->type == TYPE_VERB)
            weight = MIDCMD_VERB_WEIGHT;   // a 2nd verb rarely follows ("turn o" -> on, not open)
        *n = cand_add(cand, wt, *n, w, weight);
    }
    for (TrieNode* c = node->first_child; c != NULL; c = c->next_sibling)
        cand_collect(c, cand, wt, n, fw);
}

/*----------------------
 | g_pred_easy / g_have_solution / typeahead_set_easy
 | Description: The prediction mode set from difficulty. Easy restricts context
 |   suggestions to the solution overlay's winning path; Normal uses the full
 |   grammar but filters invalid command shapes. Both need the overlay applied;
 |   g_have_solution says whether the loaded game actually has one (if not, Easy
 |   behaves like Normal). typeahead_set_easy stores both flags.
 | Author: suinevere
 ----------------------*/
static int g_pred_easy = 0;
static int g_have_solution = 0;
void typeahead_set_easy(int easy, int have_solution) {
    g_pred_easy = easy ? 1 : 0;
    g_have_solution = have_solution ? 1 : 0;
}

/*----------------------
 | prev_links_to
 | Description: Tests whether `prev` links to `target`, setting *sol when any such
 |   link is a solution-overlay (winning-path) link.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: prev, target -- the words; sol -- receives 1 on a solution link
 | Returns: 1 if a link exists, 0 otherwise
 ----------------------*/
static int prev_links_to(DictionaryWord* prev, DictionaryWord* target, int* sol) {
    *sol = 0; int found = 0;
    for (NextWordLink* l = prev->next_words; l; l = l->next)
        if (l->target_word == target) { found = 1; if (l->solution) { *sol = 1; return 1; } }
    return found;
}

/*----------------------
 | predict_candidates
 | Description: The ranking entry point: builds an ordered suggestion list for a
 |   prefix given the previous word and first-word flag. Suppresses the four stock
 |   abbreviations the trie lacks as first words (d/u/g/x, so Accept never grows
 |   them). Collects context matches (prev -> next starting with the prefix, biased
 |   above trie completions, with movement verbs leading the compass and solution
 |   links leading even over on-screen words), surfaces on-screen nouns at an empty
 |   object slot, then adds trie completions under the prefix. In Normal mode a
 |   grammar filter drops invalid shapes (verb after verb, noun after noun, verb +
 |   non-object noun) unless the pair is a solution link. Finally the exact typed
 |   word leads its own completions, and the top `max` are selection-sorted by
 |   descending weight.
 | Author: suinevere
 | Dependencies: string.h, ctype.h
 | Globals: g_pred_easy, g_have_solution, g_hot, g_nhot
 | Params: root -- the trie; prev_word -- the preceding word (or NULL); prefix --
 |   what is typed; out -- receives the ranked words; max -- cap; first_word --
 |   true at the start of a command
 | Returns: the number of candidates written to `out`
 ----------------------*/
int predict_candidates(TrieNode* root, DictionaryWord* prev_word,
                       const char* prefix, DictionaryWord** out, int max,
                       int first_word) {
    DictionaryWord* cand[CAND_MAX];
    int wt[CAND_MAX];
    int n = 0;
    int plen = prefix ? (int) strlen(prefix) : 0;

    // d(own)/u(p)/g(again)/x(examine) have no trie word of their own, so offer
    // nothing: the letter the player typed is the whole command and passes through.
    if (first_word && plen == 1) {
        char c0 = (char) tolower((unsigned char) prefix[0]);
        if (c0 == 'd' || c0 == 'u' || c0 == 'g' || c0 == 'x') return 0;
    }

    // Easy restricts context suggestions to the winning path, but only for an
    // actual continuation of a game that has a solution overlay.
    int easy_here = g_pred_easy && g_have_solution && prev_word != NULL;

    // 1. Context matches (prev_word -> next) starting with the prefix.
    if (prev_word != NULL) {
        // A movement verb links to many directions; a verb that merely uses a
        // direction as a preposition links to one or two. Only the former leads
        // with the compass.
        int ndir = 0;
        for (NextWordLink* l = prev_word->next_words; l != NULL; l = l->next)
            if (l->target_word->type == TYPE_DIRECTION) ndir++;
        int movement = (ndir >= 4);

        for (NextWordLink* l = prev_word->next_words; l != NULL; l = l->next) {
            if (easy_here && !l->solution) continue;   // easy: winning-path words only
            if (plen == 0 || strncmp(l->target_word->text, prefix, plen) == 0) {
                DictionaryWord* tw = l->target_word;
                int w;
                if (tw->type == TYPE_DIRECTION && movement)
                    w = 900 + dir_rank(tw->text);        // lead, clockwise around compass
                else if (tw->type == TYPE_NOUN)
                    w = l->transition_weight + word_hot(tw);
                else
                    w = l->transition_weight;            // preposition, or dir-as-prep
                // Winning-path links lead even over an on-screen word.
                n = cand_add(cand, wt, n, tw, (l->solution ? 20000 : 10000) + w);
            }
        }
        // At an empty object slot, surface on-screen nouns in the context tier so a
        // thing the game just described leads over a grammar-listed unseen object.
        if (plen == 0 && !easy_here) {
            for (int i = 0; i < g_nhot; i++)
                if (g_hot[i]->type == TYPE_NOUN)
                    n = cand_add(cand, wt, n, g_hot[i],
                                 10000 + g_hot[i]->base_weight + word_hot(g_hot[i]));
        }
    }

    // 2. Trie completions under the prefix (only when something is typed).
    if (plen > 0 && !easy_here) {
        TrieNode* node = root;
        for (int i = 0; i < plen && node != NULL; i++) {
            char c = (char) tolower((unsigned char) prefix[i]);
            node = (c < 'a' || c > 'z') ? NULL : find_child(node, c);
        }
        if (node != NULL) cand_collect(node, cand, wt, &n, first_word);
    }

    // Normal-mode grammar filter: drop invalid command shapes unless the pair is
    // an explicit solution-overlay link.
    if (!easy_here && prev_word != NULL) {
        int w2 = 0;
        for (int i = 0; i < n; i++) {
            int sol = 0, has = prev_links_to(prev_word, cand[i], &sol);
            int drop = 0;
            if (!sol) {
                WordType pt = prev_word->type, ct = cand[i]->type;
                if      (pt == TYPE_VERB && ct == TYPE_VERB) drop = 1;
                else if (pt == TYPE_NOUN && ct == TYPE_NOUN) drop = 1;
                else if (pt == TYPE_VERB && ct == TYPE_NOUN && !has) drop = 1;
            }
            if (!drop) { cand[w2] = cand[i]; wt[w2] = wt[i]; w2++; }
        }
        n = w2;
    }

    // What the player has already typed, when it is a word itself, leads its own
    // completions ("n" offers "n", so Accept submits the move).
    if (plen > 0) {
        for (int i = 0; i < n; i++) {
            const char* t = cand[i]->text;
            int j = 0;
            while (j < plen && t[j] && t[j] == (char) tolower((unsigned char) prefix[j])) j++;
            if (j == plen && t[j] == '\0') { wt[i] = EXACT_MATCH_WEIGHT; break; }
        }
    }

    // Selection-sort the top `max` by descending weight.
    if (max > CAND_MAX) max = CAND_MAX;
    int count = n < max ? n : max;
    for (int i = 0; i < count; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++) if (wt[j] > wt[best]) best = j;
        DictionaryWord* tw = cand[i]; cand[i] = cand[best]; cand[best] = tw;
        int ti = wt[i]; wt[i] = wt[best]; wt[best] = ti;
        out[i] = cand[i];
    }
    return count;
}

/*----------------------
 | typeahead_set_screen
 | Description: Marks the object (noun) vocabulary appearing in `text` as on-screen
 |   for the current prompt, using a generation counter so the previous screen's
 |   marks expire for free. Only nouns are marked -- boosting prose function words
 |   would wrongly top the suggestions, and directions cycle in fixed compass order.
 | Author: suinevere
 | Dependencies: string.h
 | Globals: g_hot_gen, g_nhot, g_hot
 | Params: root -- the trie; text -- the on-screen text
 | Returns: N/A
 ----------------------*/
void typeahead_set_screen(TrieNode* root, const char* text) {
    g_hot_gen++;
    g_nhot = 0;
    if (root == NULL || text == NULL) return;
    char tok[24];
    int tp = 0;
    for (const char* p = text; ; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            if (tp < (int) sizeof(tok) - 1) tok[tp++] = c;
        } else {
            if (tp > 0) {
                tok[tp] = 0;
                DictionaryWord* w = find_exact_word(root, tok);
                if (w != NULL && w->type == TYPE_NOUN) {
                    w->hot_gen = g_hot_gen;
                    int dup = 0;
                    for (int i = 0; i < g_nhot; i++) if (g_hot[i] == w) { dup = 1; break; }
                    if (!dup && g_nhot < HOT_MAX) g_hot[g_nhot++] = w;
                }
                tp = 0;
            }
            if (c == 0) break;
        }
    }
}

/*----------------------
 | ABBREV_DIR_WEIGHT / ABBREV_VERB_WEIGHT
 | Description: Base weights for a synthesized abbreviation, mirroring
 |   typeahead_extract.c's part-of-speech priors. A direction's rank is recomputed
 |   at suggest time, so its weight only feeds the best_completion cache; a verb's
 |   sits at the plain-verb prior so a common full spelling ("look") still leads its
 |   abbreviation ("l").
 | Author: suinevere
 ----------------------*/
#define ABBREV_DIR_WEIGHT  48
#define ABBREV_VERB_WEIGHT 46

/*----------------------
 | typeahead_add_abbreviations
 | Description: Inserts the twelve stock abbreviations (n..nw, l, i, q, z) so they
 |   are always offered as a first word. Each only ever appears first (where neither
 |   mode filters trie completions), so being suggested is purely a matter of being
 |   in the trie. A missing one is inserted; one present but TYPE_UNKNOWN (e.g. a
 |   dropped "s"/"ne" reinserted by the solution overlay, or a dictionary word the
 |   extractor could not classify) is classified in place -- mutating rather than
 |   re-inserting keeps whatever links it earned; one the story already classified
 |   is trusted over the guess.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: N/A
 | Params: root -- the trie to populate
 | Returns: N/A
 ----------------------*/
void typeahead_add_abbreviations(TrieNode* root) {
    static const struct { const char* text; WordType type; int weight; } ABBREVS[] = {
        { "n",  TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "ne", TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "e",  TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "se", TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "s",  TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "sw", TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "w",  TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "nw", TYPE_DIRECTION, ABBREV_DIR_WEIGHT },
        { "l",  TYPE_VERB,      ABBREV_VERB_WEIGHT },   // look
        { "i",  TYPE_VERB,      ABBREV_VERB_WEIGHT },   // inventory
        { "q",  TYPE_VERB,      ABBREV_VERB_WEIGHT },   // quit
        { "z",  TYPE_VERB,      ABBREV_VERB_WEIGHT },   // wait
    };
    if (root == NULL) return;
    for (int i = 0; i < (int)(sizeof(ABBREVS) / sizeof(ABBREVS[0])); i++) {
        DictionaryWord* w = find_exact_word(root, ABBREVS[i].text);
        if (w == NULL) {
            insert_trie(root, create_word(ABBREVS[i].text, ABBREVS[i].type, ABBREVS[i].weight));
        } else if (w->type == TYPE_UNKNOWN) {
            w->type = ABBREVS[i].type;
            if (ABBREVS[i].weight > w->base_weight) w->base_weight = ABBREVS[i].weight;
        }
    }
}

/*----------------------
 | find_exact_word
 | Description: Walks the trie for an exact (all-letter) match of `text`, used when
 |   the player accepts/completes a word.
 | Author: suinevere
 | Dependencies: string.h, ctype.h
 | Globals: N/A
 | Params: root -- the trie; text -- the word to look up
 | Returns: the DictionaryWord, or NULL if not present
 ----------------------*/
DictionaryWord* find_exact_word(TrieNode* root, const char* text) {
    TrieNode* current = root;
    int len = strlen(text);
    for (int i = 0; i < len; i++) {
        char c = (char)tolower((unsigned char)text[i]);
        if (c < 'a' || c > 'z') return NULL;
        current = find_child(current, c);
        if (current == NULL) return NULL;
    }
    return current->word_data;
}
