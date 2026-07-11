#ifndef TYPEAHEAD_ZORK_H
#define TYPEAHEAD_ZORK_H

#include "typeahead.h"

#ifdef __cplusplus
extern "C" {
#endif

// Populate an existing (empty) trie root with the Zork I vocabulary: verbs,
// nouns, directions and prepositions, plus the common word-to-word transitions
// (e.g. "take" -> "lamp", "go" -> "north", "kill" -> "troll", "with" ->
// "sword"). The caller owns the root; this only inserts words and links.
void build_zork_typeahead(TrieNode* root);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_ZORK_H
