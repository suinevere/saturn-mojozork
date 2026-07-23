/*----------------------
 | typeahead_extract.h
 | Description: The runtime typeahead builder's contract -- decode a loaded v3
 |   story's parser model and populate the trie. Implemented in typeahead_extract.c.
 | Author: suinevere
 | Dependencies: typeahead.h (the trie/word types)
 ----------------------*/
#ifndef TYPEAHEAD_EXTRACT_H
#define TYPEAHEAD_EXTRACT_H

#include "typeahead.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | build_typeahead_from_story
 | Description: Builds the typeahead trie for a loaded Z-machine v3 story entirely
 |   at runtime: decodes the parser dictionary (with full-word recovery from object
 |   names) and derives context transitions from the game's own grammar
 |   (verb->preposition, verb/prep->the object class it expects). No pre-generated
 |   table; works for any v3 game. `root` must be a fresh, empty trie node.
 | Author: suinevere
 ----------------------*/
void build_typeahead_from_story(TrieNode* root, const unsigned char* story, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_EXTRACT_H
