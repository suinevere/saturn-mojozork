#ifndef TYPEAHEAD_EXTRACT_H
#define TYPEAHEAD_EXTRACT_H

#include "typeahead.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build the typeahead trie for a loaded Z-machine v3 story image, entirely at
// runtime: decode the parser dictionary (with full-word recovery from object
// names), and derive context transitions from the game's own grammar --
// verb->preposition and verb/preposition->the object class it expects
// (eat->food, open->openables, with->weapons). No pre-generated table needed;
// works for any v3 game. `root` must be a fresh, empty trie node.
void build_typeahead_from_story(TrieNode* root, const unsigned char* story, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_EXTRACT_H
