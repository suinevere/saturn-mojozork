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
        int weight = w->base_weight;
        if (fw && (w->type == TYPE_VERB || w->type == TYPE_DIRECTION))
            weight += FIRST_WORD_POS_BONUS;
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
        for (NextWordLink* l = prev_word->next_words; l != NULL; l = l->next) {
            if (plen == 0 || strncmp(l->target_word->text, prefix, plen) == 0)
                n = cand_add(cand, wt, n, l->target_word, 10000 + l->transition_weight);
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
