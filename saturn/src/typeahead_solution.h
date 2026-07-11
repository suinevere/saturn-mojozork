#ifndef TYPEAHEAD_SOLUTION_H
#define TYPEAHEAD_SOLUTION_H

#include "typeahead.h"

#ifdef __cplusplus
extern "C" {
#endif

// Apply the compiled "solution" overlay for the loaded story, if one matches its
// release + serial. Boosts base weights and adds winning-path transitions on top
// of the grammar layer built by build_typeahead_from_story(). No-op when the game
// has no bundled solution. Call after the grammar build.
void apply_solution_overlay(TrieNode* root, const unsigned char* story, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif // TYPEAHEAD_SOLUTION_H
