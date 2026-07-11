#ifndef TYPEAHEAD_H
#define TYPEAHEAD_H

#ifdef __cplusplus
extern "C" {
#endif

#define ALPHABET_SIZE 26

// Memory allocation wrappers for the generalized library.
// By default we hook into externally provided functions.
#ifndef TYPEAHEAD_MALLOC
extern void* typeahead_malloc(unsigned int size);
extern void typeahead_free(void* ptr);
#define TYPEAHEAD_MALLOC typeahead_malloc
#define TYPEAHEAD_FREE typeahead_free
#endif

typedef enum {
    TYPE_UNKNOWN = 0,
    TYPE_VERB,
    TYPE_NOUN,
    TYPE_DIRECTION,
    TYPE_PREP
} WordType;

typedef struct DictionaryWord DictionaryWord;
typedef struct NextWordLink NextWordLink;

struct NextWordLink {
    DictionaryWord* target_word;
    int transition_weight;
    NextWordLink* next;
};

struct DictionaryWord {
    char* text;
    WordType type;
    int base_weight;
    NextWordLink* next_words;
};

// Trie node using a first-child / next-sibling layout instead of a dense
// children[26] array. English words branch sparsely, so a 26-pointer array
// wastes ~100 bytes per node; storing only the children that exist cuts the
// node from ~112 to ~20 bytes (~5x less RAM for a full game dictionary). The
// letter this node represents lives in `letter`; the root's is unused.
typedef struct TrieNode {
    struct TrieNode* first_child;    // head of this node's child list
    struct TrieNode* next_sibling;   // next child of this node's parent
    char letter;                     // 'a'..'z' for this node (0 for the root)
    DictionaryWord* word_data;       // set if a word ends exactly here
    DictionaryWord* best_completion; // heaviest word passing through this prefix
} TrieNode;

// Library functions
DictionaryWord* create_word(const char* text, WordType type, int weight);
void add_next_word(DictionaryWord* source, DictionaryWord* target, int weight);
TrieNode* create_trie_node();
void insert_trie(TrieNode* root, DictionaryWord* word);
DictionaryWord* predict_with_context(TrieNode* root, DictionaryWord* prev_word, const char* prefix);
DictionaryWord* find_exact_word(TrieNode* root, const char* text);

// Fill `out` (capacity `max`) with candidate completions for `prefix` given the
// `prev_word` context, ranked by descending weight (context matches first, then
// trie completions by base weight). Returns how many were written. Used by the
// UI to let the player cycle through suggestions.
//
// When `first_word` is set (the current word starts the command), verbs and
// directions -- the only parts of speech that can begin a command -- are ranked
// above nouns/prepositions/adjectives, so "o" offers "open" before "oil".
int predict_candidates(TrieNode* root, DictionaryWord* prev_word,
                       const char* prefix, DictionaryWord** out, int max,
                       int first_word);

// Free an entire trie built by the above: every node, and each word's text and
// next-word links. Each DictionaryWord is one node's word_data, so this frees
// everything exactly once. Use before rebuilding for a newly loaded story.
void destroy_typeahead(TrieNode* root);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_H
