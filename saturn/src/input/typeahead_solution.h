/*----------------------
 | typeahead_solution.h
 | Description: The compiled walkthrough-overlay's contract -- boost the trie built
 |   by build_typeahead_from_story with a matching game's winning-path weights.
 |   Implemented in typeahead_solution.c (generated).
 | Author: suinevere
 | Dependencies: typeahead.h (the trie/word types)
 ----------------------*/
#ifndef TYPEAHEAD_SOLUTION_H
#define TYPEAHEAD_SOLUTION_H

#include "typeahead.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | apply_solution_overlay
 | Description: Applies the compiled "solution" overlay for the loaded story if one
 |   matches its release + serial: boosts base weights and adds winning-path
 |   transitions on top of the grammar layer. A no-op when the game has no bundled
 |   solution; call after the grammar build. Returns 1 if a solution matched, else 0.
 | Author: suinevere
 ----------------------*/
int apply_solution_overlay(TrieNode* root, const unsigned char* story, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_SOLUTION_H
