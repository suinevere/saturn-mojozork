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

typedef struct TrieNode {
    struct TrieNode* children[ALPHABET_SIZE];
    DictionaryWord* word_data;
    DictionaryWord* best_completion;
} TrieNode;

// Library functions
DictionaryWord* create_word(const char* text, WordType type, int weight);
void add_next_word(DictionaryWord* source, DictionaryWord* target, int weight);
TrieNode* create_trie_node();
void insert_trie(TrieNode* root, DictionaryWord* word);
DictionaryWord* predict_with_context(TrieNode* root, DictionaryWord* prev_word, const char* prefix);
DictionaryWord* find_exact_word(TrieNode* root, const char* text);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_H
