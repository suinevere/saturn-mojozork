#include "typeahead.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char* typeahead_strdup(const char* s) {
    if (!s) return NULL;
    int len = strlen(s);
    char* copy = (char*)TYPEAHEAD_MALLOC(len + 1);
    if (copy) {
        strcpy(copy, s);
    }
    return copy;
}

// Create a new Dictionary Word
DictionaryWord* create_word(const char* text, WordType type, int weight) {
    DictionaryWord* word = (DictionaryWord*)TYPEAHEAD_MALLOC(sizeof(DictionaryWord));
    word->text = typeahead_strdup(text);
    word->type = type;
    word->base_weight = weight;
    word->next_words = NULL;
    word->hot_gen = 0;
    return word;
}

// Create a directional association (Word A is likely followed by Word B)
void add_next_word(DictionaryWord* source, DictionaryWord* target, int weight) {
    NextWordLink* link = (NextWordLink*)TYPEAHEAD_MALLOC(sizeof(NextWordLink));
    link->target_word = target;
    link->transition_weight = weight;
    // Insert at the head of the linked list
    link->next = source->next_words;
    source->next_words = link;
}

// Create an empty Trie Node
TrieNode* create_trie_node() {
    TrieNode* node = (TrieNode*)TYPEAHEAD_MALLOC(sizeof(TrieNode));
    node->first_child = NULL;
    node->next_sibling = NULL;
    node->letter = 0;
    node->word_data = NULL;
    node->best_completion = NULL;
    return node;
}

// Find the child of 'node' for letter 'c' (lowercase a-z), or NULL. Walks the
// sibling list -- short for English tries (small branching factor).
static TrieNode* find_child(TrieNode* node, char c) {
    for (TrieNode* ch = node->first_child; ch != NULL; ch = ch->next_sibling) {
        if (ch->letter == c) return ch;
    }
    return NULL;
}

// Insert a word into the Trie and update 'best_completion' caches
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

        // OPTIMIZATION: Update the cache as we traverse down
        // If this node has no best completion, or the new word is heavier, swap it.
        if (current->best_completion == NULL ||
            word->base_weight > current->best_completion->base_weight) {
            current->best_completion = word;
        }
    }
    // Mark the end of the word
    current->word_data = word;
}

// Context-aware prediction combining prefix and previous word
DictionaryWord* predict_with_context(TrieNode* root, DictionaryWord* prev_word, const char* prefix) {
    if (prefix == NULL || strlen(prefix) == 0) {
        // Just predict based on context if no prefix
        if (prev_word && prev_word->next_words) {
            DictionaryWord* best = NULL;
            int best_w = -1;
            for (NextWordLink* curr = prev_word->next_words; curr != NULL; curr = curr->next) {
                if (curr->transition_weight > best_w) {
                    best_w = curr->transition_weight;
                    best = curr->target_word;
                }
            }
            return best;
        }
        return NULL;
    }

    int prefix_len = strlen(prefix);
    DictionaryWord* best_context_word = NULL;
    int best_context_weight = -1;

    // 1. Check context (next_words) first
    if (prev_word != NULL) {
        NextWordLink* current = prev_word->next_words;
        while (current != NULL) {
            if (strncmp(current->target_word->text, prefix, prefix_len) == 0) {
                if (current->transition_weight > best_context_weight) {
                    best_context_weight = current->transition_weight;
                    best_context_word = current->target_word;
                }
            }
            current = current->next;
        }
    }

    // Return the best context match if we found one
    if (best_context_word != NULL) {
        return best_context_word;
    }

    // 2. Fallback to Trie base_weight prediction
    TrieNode* current_node = root;
    for (int i = 0; i < prefix_len; i++) {
        char c = (char)tolower((unsigned char)prefix[i]);
        if (c < 'a' || c > 'z') return NULL;
        current_node = find_child(current_node, c);
        if (current_node == NULL) return NULL; // No matches
    }

    return current_node->best_completion;
}

// Recursively free the trie, plus each word's links and text. A DictionaryWord
// is the word_data of exactly one node, so every allocation is freed once.
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

#define CAND_MAX 32

// Bonus that lifts verbs/directions above other parts of speech at the start of
// a command (well above any base weight, but below a context match's 10000).
#define FIRST_WORD_POS_BONUS 2000

// Bonus for a word currently visible on screen -- enough to top its tier (base
// weights and context transitions differ by far less) without crossing tiers.
#define SCREEN_BONUS 500
#define HOT_MAX 64

static int g_hot_gen = 0;                 // bumped each time the screen is set
static DictionaryWord* g_hot[HOT_MAX];    // on-screen words, for empty-prefix surfacing
static int g_nhot = 0;

static int word_hot(DictionaryWord* w) {
    return (g_hot_gen != 0 && w->hot_gen == g_hot_gen) ? SCREEN_BONUS : 0;
}

// Rank a direction word for clockwise-compass ordering: north, northeast, east,
// ... northwest, then up, down, in, out. Higher sorts earlier. The full/6-char
// spelling outranks its abbreviation (north above n). 0 if not a direction.
// Covers v3 dictionary forms including 6-char truncations (northe = northeast).
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

// A "compass" exit (leads verbs at the first word). In/out and any oddballs are
// excluded, so a lone "o" still offers "open" rather than "out".
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

// Base for a direction's compass weight -- keeps it in the verb tier so common
// verbs still lead their prefix ("o" -> open, not out).
#define DIR_BASE 40

// Weight for a verb suggested mid-command (not the first word). Below the base
// weight of prepositions/nouns/directions so they lead, but still present so the
// player can cycle to it. Two verbs in a row is almost never a valid command.
#define MIDCMD_VERB_WEIGHT 5

// Add `w` at `weight`, de-duplicating by pointer (keeping the higher weight).
// When the arrays are full, keep the top CAND_MAX by weight -- evict the current
// minimum if this candidate is heavier -- so a busy prefix never drops the
// best-ranked word just because it was reached late in the traversal.
static int cand_add(DictionaryWord** cand, int* wt, int n, DictionaryWord* w, int weight) {
    for (int i = 0; i < n; i++)
        if (cand[i] == w) { if (weight > wt[i]) wt[i] = weight; return n; }
    if (n < CAND_MAX) { cand[n] = w; wt[n] = weight; return n + 1; }
    int mi = 0;
    for (int i = 1; i < n; i++) if (wt[i] < wt[mi]) mi = i;
    if (weight > wt[mi]) { cand[mi] = w; wt[mi] = weight; }
    return n;
}

// Depth-first collect every word in this subtree. `fw` (first word) lifts verbs
// and directions so they rank above nouns/prepositions at the start of a command.
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
                // An on-screen object in a mid-command slot leads even a grammar-
                // listed off-screen object: lift it into the context tier (matches
                // predict_candidates' empty-object-slot handling).
                if (!fw && hot) weight += 10000;
            }
        }
        if (fw && (w->type == TYPE_VERB || w->type == TYPE_DIRECTION))
            weight += FIRST_WORD_POS_BONUS;
        else if (!fw && w->type == TYPE_VERB)
            weight = MIDCMD_VERB_WEIGHT;   // a 2nd verb rarely follows -> rank below
                                           // prepositions/nouns ("turn o" -> on, not open)
        *n = cand_add(cand, wt, *n, w, weight);
    }
    for (TrieNode* c = node->first_child; c != NULL; c = c->next_sibling)
        cand_collect(c, cand, wt, n, fw);
}

int predict_candidates(TrieNode* root, DictionaryWord* prev_word,
                       const char* prefix, DictionaryWord** out, int max,
                       int first_word) {
    DictionaryWord* cand[CAND_MAX];
    int wt[CAND_MAX];
    int n = 0;
    int plen = prefix ? (int) strlen(prefix) : 0;

    // 1. Context matches (prev_word -> next) that start with the prefix. Bias them
    //    above trie completions so the context-aware suggestion sorts first.
    if (prev_word != NULL) {
        // A movement verb (go/walk) links to many directions as its object; a verb
        // that merely uses a direction word as a preposition ("drop it down") links
        // to only one or two. Only the former should lead with the compass.
        int ndir = 0;
        for (NextWordLink* l = prev_word->next_words; l != NULL; l = l->next)
            if (l->target_word->type == TYPE_DIRECTION) ndir++;
        int movement = (ndir >= 4);

        for (NextWordLink* l = prev_word->next_words; l != NULL; l = l->next) {
            if (plen == 0 || strncmp(l->target_word->text, prefix, plen) == 0) {
                DictionaryWord* tw = l->target_word;
                int w;
                if (tw->type == TYPE_DIRECTION && movement)
                    w = 900 + dir_rank(tw->text);        // lead, clockwise around compass
                else if (tw->type == TYPE_NOUN)
                    w = l->transition_weight + word_hot(tw);
                else
                    w = l->transition_weight;            // preposition, or dir-as-prep
                n = cand_add(cand, wt, n, tw, 10000 + w);
            }
        }
        // At an empty object slot, surface on-screen nouns IN THE CONTEXT TIER so a
        // thing the game just described leads, even over a grammar-listed object the
        // player can't see (e.g. "turn on " -> the visible "computer", not "waxer").
        // Same 10000+ base as the grammar links above, plus the on-screen bonus.
        if (plen == 0) {
            for (int i = 0; i < g_nhot; i++)
                if (g_hot[i]->type == TYPE_NOUN)
                    n = cand_add(cand, wt, n, g_hot[i],
                                 10000 + g_hot[i]->base_weight + word_hot(g_hot[i]));
        }
    }

    // 2. Trie completions under the prefix (only when something has been typed).
    if (plen > 0) {
        TrieNode* node = root;
        for (int i = 0; i < plen && node != NULL; i++) {
            char c = (char) tolower((unsigned char) prefix[i]);
            node = (c < 'a' || c > 'z') ? NULL : find_child(node, c);
        }
        if (node != NULL) cand_collect(node, cand, wt, &n, first_word);
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

// Mark vocabulary words appearing in `text` as on-screen for the current prompt.
// Uses a generation counter so the previous screen's marks expire for free.
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
                // Only objects (nouns): boosting prose function words like "with"
                // in "with a boarded door" would wrongly top the suggestions.
                // Directions cycle in fixed compass order, not by screen/frequency.
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

// Find exact dictionary word (used when user hits space)
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
