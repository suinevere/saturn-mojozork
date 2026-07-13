/*----------------------
 | typeahead.h
 | Description: The typeahead prediction library's types and API: the letter trie
 |   of DictionaryWords with weighted transition links, and the ranking that turns
 |   a prefix (plus context and on-screen words) into an ordered candidate list.
 |   Implemented in typeahead.c; the story-driven build and walkthrough overlay in
 |   typeahead_extract.c / typeahead_solution.c.
 | Author: suinevere
 | Dependencies: an external allocator via TYPEAHEAD_MALLOC/FREE (the Saturn client
 |   provides typeahead_malloc/free; host tests substitute their own)
 ----------------------*/
#ifndef TYPEAHEAD_H
#define TYPEAHEAD_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | ALPHABET_SIZE / TYPEAHEAD_MALLOC / TYPEAHEAD_FREE
 | Description: The trie's alphabet size, and the allocator hooks: by default the
 |   library links against externally provided typeahead_malloc/free, overridable
 |   by predefining the macros.
 | Author: suinevere
 ----------------------*/
#define ALPHABET_SIZE 26

#ifndef TYPEAHEAD_MALLOC
extern void* typeahead_malloc(unsigned int size);
extern void typeahead_free(void* ptr);
#define TYPEAHEAD_MALLOC typeahead_malloc
#define TYPEAHEAD_FREE typeahead_free
#endif

/*----------------------
 | WordType
 | Description: A word's part of speech (unknown, verb, noun, direction,
 |   preposition), which drives the ranking tiers.
 | Author: suinevere
 ----------------------*/
typedef enum {
    TYPE_UNKNOWN = 0,
    TYPE_VERB,
    TYPE_NOUN,
    TYPE_DIRECTION,
    TYPE_PREP
} WordType;

typedef struct DictionaryWord DictionaryWord;
typedef struct NextWordLink NextWordLink;

/*----------------------
 | NextWordLink / DictionaryWord
 | Description: NextWordLink is a weighted directional edge (source likely followed
 |   by target_word; solution == 1 marks a winning-path overlay link). DictionaryWord
 |   is a vocabulary word: its text, part of speech, base weight, transition list,
 |   and hot_gen (equal to the current screen generation while it is on-screen).
 | Author: suinevere
 ----------------------*/
struct NextWordLink {
    DictionaryWord* target_word;
    int transition_weight;
    char solution;           // 1 = a winning-path link from the solution overlay
    NextWordLink* next;
};

struct DictionaryWord {
    char* text;
    WordType type;
    int base_weight;
    NextWordLink* next_words;
    int hot_gen;             // == the current screen generation when on-screen now
};

/*----------------------
 | TrieNode
 | Description: A trie node in first-child / next-sibling layout rather than a
 |   dense children[26] array -- English words branch sparsely, so storing only the
 |   children that exist cuts a node from ~112 to ~20 bytes (~5x less RAM for a full
 |   dictionary). `letter` is this node's 'a'..'z' (0 for the root), word_data is
 |   set when a word ends exactly here, and best_completion caches the heaviest word
 |   passing through this prefix.
 | Author: suinevere
 ----------------------*/
typedef struct TrieNode {
    struct TrieNode* first_child;    // head of this node's child list
    struct TrieNode* next_sibling;   // next child of this node's parent
    char letter;                     // 'a'..'z' for this node (0 for the root)
    DictionaryWord* word_data;       // set if a word ends exactly here
    DictionaryWord* best_completion; // heaviest word passing through this prefix
} TrieNode;

/*----------------------
 | trie construction (create_word / add_next_word / add_solution_link /
 | create_trie_node / insert_trie / find_exact_word)
 | Description: Build and query the trie: create a word or an empty node; link two
 |   words (add_solution_link flags a winning-path overlay edge that outranks
 |   on-screen words); insert a word into the trie; and find an exact word by text.
 | Author: suinevere
 ----------------------*/
DictionaryWord* create_word(const char* text, WordType type, int weight);
void add_next_word(DictionaryWord* source, DictionaryWord* target, int weight);
void add_solution_link(DictionaryWord* source, DictionaryWord* target, int weight);
TrieNode* create_trie_node();
void insert_trie(TrieNode* root, DictionaryWord* word);
DictionaryWord* find_exact_word(TrieNode* root, const char* text);

/*----------------------
 | predict_candidates
 | Description: Fills `out` (capacity `max`) with ranked completions for `prefix`
 |   given the `prev_word` context (context matches first, then trie completions by
 |   base weight), returning how many were written -- the list the UI cycles. When
 |   `first_word` is set, verbs and directions (the only parts of speech that can
 |   start a command) rank above nouns/prepositions, so "o" offers "open" before
 |   "oil".
 | Author: suinevere
 ----------------------*/
int predict_candidates(TrieNode* root, DictionaryWord* prev_word,
                       const char* prefix, DictionaryWord** out, int max,
                       int first_word);

/*----------------------
 | typeahead_set_easy
 | Description: Sets the prediction mode. easy restricts context to the winning
 |   path (only when have_solution); normal is full grammar with an invalid-shape
 |   filter. Both keep solution-overlay links; with have_solution 0, easy behaves
 |   like normal.
 | Author: suinevere
 ----------------------*/
void typeahead_set_easy(int easy, int have_solution);

/*----------------------
 | destroy_typeahead
 | Description: Frees an entire trie -- every node, and each word's text and links.
 |   Each DictionaryWord is one node's word_data, so everything is freed exactly
 |   once. Use before rebuilding for a newly loaded story.
 | Author: suinevere
 ----------------------*/
void destroy_typeahead(TrieNode* root);

/*----------------------
 | typeahead_add_abbreviations
 | Description: Ensures the twelve stock abbreviations (n ne e se s sw w nw l i q z)
 |   are in the trie so they are always accepted and suggested whatever the story's
 |   dictionary defines. Words already present keep their grammar links. Call after
 |   the story/solution layers.
 | Author: suinevere
 ----------------------*/
void typeahead_add_abbreviations(TrieNode* root);

/*----------------------
 | typeahead_set_screen
 | Description: Marks the vocabulary words in `text` (e.g. the visible room text)
 |   as on-screen, so predict_candidates ranks them above their peers -- an object
 |   the game just mentioned leads its suggestions. Call once per prompt.
 | Author: suinevere
 ----------------------*/
void typeahead_set_screen(TrieNode* root, const char* text);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_H
