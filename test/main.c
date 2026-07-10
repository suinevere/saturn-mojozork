#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>

#define ALPHABET_SIZE 26

// ==========================================
// 1. STRUCTURE DEFINITIONS
// ==========================================

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


// ==========================================
// 2. HELPER FUNCTIONS (Memory & Setup)
// ==========================================

// Create a new Dictionary Word
DictionaryWord* create_word(const char* text, WordType type, int weight) {
    DictionaryWord* word = (DictionaryWord*)malloc(sizeof(DictionaryWord));
    word->text = strdup(text);
    word->type = type;
    word->base_weight = weight;
    word->next_words = NULL;
    return word;
}

// Create a directional association (Word A is likely followed by Word B)
void add_next_word(DictionaryWord* source, DictionaryWord* target, int weight) {
    NextWordLink* link = (NextWordLink*)malloc(sizeof(NextWordLink));
    link->target_word = target;
    link->transition_weight = weight;
    // Insert at the head of the linked list
    link->next = source->next_words;
    source->next_words = link;
}

// Create an empty Trie Node
TrieNode* create_trie_node() {
    TrieNode* node = (TrieNode*)malloc(sizeof(TrieNode));
    node->word_data = NULL;
    node->best_completion = NULL;
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        node->children[i] = NULL;
    }
    return node;
}


// ==========================================
// 3. CORE LOGIC (Trie & Prediction)
// ==========================================

// Insert a word into the Trie and update 'best_completion' caches
void insert_trie(TrieNode* root, DictionaryWord* word) {
    TrieNode* current = root;
    int len = strlen(word->text);

    for (int i = 0; i < len; i++) {
        int index = tolower(word->text[i]) - 'a';
        if (index < 0 || index >= ALPHABET_SIZE) continue; // Skip non-letters

        if (current->children[index] == NULL) {
            current->children[index] = create_trie_node();
        }
        current = current->children[index];

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
        int index = tolower(prefix[i]) - 'a';
        if (index < 0 || index >= ALPHABET_SIZE || current_node->children[index] == NULL) {
            return NULL; // No matches
        }
        current_node = current_node->children[index];
    }

    return current_node->best_completion;
}

// Find exact dictionary word (used when user hits space)
DictionaryWord* find_exact_word(TrieNode* root, const char* text) {
    TrieNode* current = root;
    int len = strlen(text);
    for (int i = 0; i < len; i++) {
        int index = tolower(text[i]) - 'a';
        if (index < 0 || index >= ALPHABET_SIZE || current->children[index] == NULL) return NULL;
        current = current->children[index];
    }
    return current->word_data;
}

// ==========================================
// 4. MAIN DEMONSTRATION
// ==========================================

int main() {
    // 1. Initialize Root
    TrieNode* root = create_trie_node();

    // 2. Create the Dictionary
    DictionaryWord* w_take    = create_word("take", TYPE_VERB, 100);
    DictionaryWord* w_tall    = create_word("tall", TYPE_UNKNOWN, 10);
    DictionaryWord* w_look    = create_word("look", TYPE_VERB, 90);
    DictionaryWord* w_sword   = create_word("sword", TYPE_NOUN, 50);
    DictionaryWord* w_lantern = create_word("lantern", TYPE_NOUN, 80);
    DictionaryWord* w_around  = create_word("around", TYPE_DIRECTION, 40);
    DictionaryWord* w_at      = create_word("at", TYPE_PREP, 30);

    // 3. Link Associations (Markov Chain / Bigram logic)
    // "Take" is heavily followed by "lantern" and "sword"
    add_next_word(w_take, w_lantern, 85);
    add_next_word(w_take, w_sword, 60);

    // "Look" is heavily followed by "around" or "at"
    add_next_word(w_look, w_around, 95);
    add_next_word(w_look, w_at, 50);

    // 4. Load into Trie
    insert_trie(root, w_take);
    insert_trie(root, w_tall);
    insert_trie(root, w_look);
    insert_trie(root, w_sword);
    insert_trie(root, w_lantern);
    insert_trie(root, w_around);
    insert_trie(root, w_at);

    // 5. Interactive Simulation loop
    printf("--- INTERACTIVE TYPE-AHEAD ---\n");
    printf("Type letters to see predictions. Press TAB to accept autocomplete.\n");
    printf("Press SPACE to finish a word. Press ENTER to quit.\n\n");

    char current_word[256] = {0};
    int char_idx = 0;
    DictionaryWord* prev_word = NULL;
    DictionaryWord* prediction = NULL;
    
    printf("> ");
    while (1) {
        char c = _getch();
        
        if (c == '\r' || c == '\n') {
            break;
        } else if (c == '\t') {
            // Accept autocomplete
            if (prediction != NULL && strlen(prediction->text) > char_idx) {
                const char* remaining = prediction->text + char_idx;
                strcpy(current_word, prediction->text);
                char_idx = strlen(current_word);
                printf("\033[0m%s", remaining); // Print remaining in normal color
            }
            // Update prediction based on the newly accepted word
            prediction = predict_with_context(root, prev_word, current_word);
            continue; 
        } else if (c == '\b') {
            if (char_idx > 0) {
                current_word[--char_idx] = '\0';
                printf("\b \b");
            }
        } else if (c == ' ') {
            // Finish current word, set as previous word
            DictionaryWord* found = find_exact_word(root, current_word);
            if (found) {
                prev_word = found;
            } else {
                // If the word isn't in dictionary, we lose context
                prev_word = NULL; 
            }
            
            memset(current_word, 0, sizeof(current_word));
            char_idx = 0;
            printf(" ");
        } else if (isalpha(c)) {
            current_word[char_idx++] = c;
            printf("%c", c);
        }

        // Output current prediction in-place
        prediction = predict_with_context(root, prev_word, current_word);
        
        // Clear previous prediction using spaces (universal fallback for \033[K)
        printf("                    ");
        for (int i = 0; i < 20; i++) printf("\b");
        
        if (prediction != NULL && strlen(prediction->text) > char_idx) {
            const char* suffix = prediction->text + char_idx;
            int suffix_len = strlen(suffix);
            
            // Print the predicted suffix in grey
            printf("\033[90m%s\033[0m", suffix);
            
            // Move cursor back to the active typing position
            for (int i = 0; i < suffix_len; i++) {
                printf("\b");
            }
        }
    }

    printf("\n\nExiting...\n");
    return 0;
}