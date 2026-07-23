// Runtime typeahead builder: decode a loaded Z-machine v3 story's parser model
// (dictionary + verb grammar + object attributes) and populate the trie. This is
// the on-device port of tools/typeahead/gen_typeahead.py (Layers 1a-1c); see that
// file and tools/typeahead/README.md for the format details.

#include "typeahead_extract.h"
#include <string.h>
#include <ctype.h>

// Dictionary part-of-speech flag bits (Infocom v3).
#define FL_NOUN 0x80
#define FL_VERB 0x40
#define FL_ADJ  0x20
#define FL_DIR  0x10
#define FL_PREP 0x08

// Grammar-derived transition weights (baseline priors; a solution overlay could
// later raise specific winning-path pairs above these).
#define W_VERB_PREP 60
#define W_VERB_NOUN 55
#define W_PREP_NOUN 55
#define W_ADJ_NOUN  52
#define MAX_CLASS   40     // skip verb/prep->noun links for classes larger than this
// Part-of-speech base-weight priors. Verbs and directions sit close together so a
// rarely-used direction (out/up) doesn't outrank a common verb; the solution
// overlay/frequency then ranks the directions that actually get typed (n/s/e/w).
#define BASE_DIR    48
#define BASE_VERB   46
#define BASE_DEF    30

// Build-time limits (Sorcerer, the largest v3 game here, has ~1012 dict words).
#define MAXW      1300
#define MAXOBJ    500
#define NAMELEN   48
#define CLASSCAP  64

// The standard interactive-fiction verb set, most-common first. These lead the
// first-word suggestions ahead of the game's obscure verbs. Matched against the
// dictionary form (which may be truncated to 6 chars), so "examine" also hits the
// stored "examin", "inventory" hits "invent", etc.
static const char* const COMMON_VERBS[] = {
    "examine", "push", "take", "pull", "drop", "turn", "open", "feel", "put",
    "eat", "climb", "drink", "wave", "fill", "wear", "smell", "listen", "break",
    "dig", "burn", "enter", "look", "search", "unlock",
    "jump", "sleep", "pray", "wake", "curse", "undo", "sing", "read", "talk",
    "ask", "tell", "give", "show", "inventory", "wait",
};

// Base weight for a common verb (0 if not one). Earlier in the list -> heavier;
// all stay above the default verb prior so any listed verb leads the obscure ones.
static int common_verb_weight(const char* t) {
    int tl = (int) strlen(t);
    int nc = (int)(sizeof(COMMON_VERBS) / sizeof(COMMON_VERBS[0]));
    for (int i = 0; i < nc; i++) {
        const char* c = COMMON_VERBS[i];
        if (strcmp(t, c) == 0 || (tl == 6 && strncmp(t, c, 6) == 0)) {
            int w = 88 - i;
            return w < 58 ? 58 : w;
        }
    }
    return 0;
}

// Bare direction abbreviations -- dropped here so the full word is what the
// grammar layer offers. The compass eight are put back afterwards by
// typeahead_add_abbreviations (they rank below their full spelling, so "n" still
// suggests "north" first); "u"/"d" are the only ones this really removes.
static int is_dir_abbrev(const char* t) {
    return !strcmp(t, "n") || !strcmp(t, "s") || !strcmp(t, "e") || !strcmp(t, "w")
        || !strcmp(t, "u") || !strcmp(t, "d") || !strcmp(t, "ne") || !strcmp(t, "nw")
        || !strcmp(t, "se") || !strcmp(t, "sw");
}

// Expand a 6-char truncated diagonal to its full name (parser still matches the
// first 6 chars), so suggestions read "southwest" rather than "southw".
static void diag_full(char* t) {
    if      (!strcmp(t, "northe")) strcpy(t, "northeast");
    else if (!strcmp(t, "northw")) strcpy(t, "northwest");
    else if (!strcmp(t, "southe")) strcpy(t, "southeast");
    else if (!strcmp(t, "southw")) strcpy(t, "southwest");
}

static const char A0[] = "abcdefghijklmnopqrstuvwxyz";
static const char A1[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
// A2 for z-chars 6..31; index 0 (z-char 6) is the 10-bit escape, handled inline.
static const char A2[] = { 0, '\n', '0','1','2','3','4','5','6','7','8','9',
                           '.', ',', '!', '?', '_', '#', '\'', '"', '/', '\\',
                           '-', ':', '(', ')' };

// Canonical spelling preference for shared preposition ids (synonyms collapse to
// one id, e.g. with/through/using all = 254). Earlier = preferred.
static const char* const PREP_PREF[] = {
    "with", "in", "on", "to", "under", "from", "at", "off", "up", "down",
    "out", "over", "behind", "for", "across", "around", "away", "of",
};

// --- working state (file-static to keep it off the stack; reused each build) ---
static char    d_text[MAXW][12];
static unsigned char d_flags[MAXW];
static unsigned char d_id[MAXW];
static DictionaryWord* d_word[MAXW];
static int     d_count;

static unsigned int   o_attrs[MAXOBJ];
static char    o_name[MAXOBJ][NAMELEN];
static int     o_count;

static DictionaryWord* attr_nouns[32][CLASSCAP];
static int     attr_n[32];
static DictionaryWord* prep_canon[256];

static const unsigned char* S;   // story image
static unsigned int         SLEN;

static unsigned int rd16(unsigned int a) { return ((unsigned int) S[a] << 8) | S[a + 1]; }

// Decode a Z-string starting at byte address `addr`, expanding abbreviations.
// Appends into buf[pos..cap) and returns the new position.
static int decode_at(unsigned int addr, unsigned int abbr, int allow_abbr,
                     char* buf, int cap, int pos);

static int emit_zchars(const unsigned char* zc, int n, unsigned int abbr,
                       int allow_abbr, char* buf, int cap, int pos) {
    int alpha = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = zc[i];
        if (c == 0) { if (pos < cap - 1) buf[pos++] = ' '; alpha = 0; continue; }
        if (c <= 3) {                                   // abbreviation reference
            if (allow_abbr && i + 1 < n) {
                int idx = 32 * (c - 1) + zc[i + 1]; i++;
                unsigned int aa = rd16(abbr + 2 * idx) * 2;
                pos = decode_at(aa, abbr, 0, buf, cap, pos);
            }
            alpha = 0; continue;
        }
        if (c == 4) { alpha = 1; continue; }
        if (c == 5) { alpha = 2; continue; }
        char ch;
        if (alpha == 0) ch = A0[c - 6];
        else if (alpha == 1) ch = A1[c - 6];
        else {
            if (c == 6) {                               // 10-bit ZSCII escape
                if (i + 2 < n) {
                    int zs = (zc[i + 1] << 5) | zc[i + 2]; i += 2;
                    if (zs >= 32 && zs < 127 && pos < cap - 1) buf[pos++] = (char) zs;
                }
                alpha = 0; continue;
            }
            ch = A2[c - 6];
        }
        if (pos < cap - 1) buf[pos++] = ch;
        alpha = 0;
    }
    buf[pos] = 0;
    return pos;
}

static int decode_at(unsigned int addr, unsigned int abbr, int allow_abbr,
                     char* buf, int cap, int pos) {
    unsigned char zc[96];
    int n = 0;
    unsigned int a = addr;
    while (n <= 90) {
        unsigned int w = rd16(a); a += 2;
        zc[n++] = (w >> 10) & 0x1f; zc[n++] = (w >> 5) & 0x1f; zc[n++] = w & 0x1f;
        if ((w & 0x8000) || a + 1 >= SLEN) break;
    }
    return emit_zchars(zc, n, abbr, allow_abbr, buf, cap, pos);
}

// Decode one dictionary word (4-byte / 6 z-char form, no abbreviations).
static void dict_word(unsigned int off, char* buf) {
    unsigned int w1 = rd16(off), w2 = rd16(off + 2);
    unsigned char zc[6] = { (unsigned char)((w1 >> 10) & 0x1f), (unsigned char)((w1 >> 5) & 0x1f),
                            (unsigned char)(w1 & 0x1f), (unsigned char)((w2 >> 10) & 0x1f),
                            (unsigned char)((w2 >> 5) & 0x1f), (unsigned char)(w2 & 0x1f) };
    int p = emit_zchars(zc, 6, 0, 0, buf, 12, 0);
    while (p > 0 && buf[p - 1] == ' ') buf[--p] = 0;   // strip padding
}

static int is_alpha_word(const char* s) {
    if (!*s) return 0;
    for (; *s; s++) if (*s < 'a' || *s > 'z') return 0;
    return 1;
}

// Look up a dictionary entry's flags by (already-lowercase) word; 0 if unknown.
static unsigned char flags_of(const char* w) {
    for (int i = 0; i < d_count; i++)
        if (strcmp(d_text[i], w) == 0) return d_flags[i];
    return 0;
}

// Tokenise a display name into lowercase [a-z]+ tokens; returns token count and
// fills tok[][]. Non-letters split tokens.
static int tokenize(const char* name, char tok[][NAMELEN], int maxtok) {
    int nt = 0, tp = 0;
    for (const char* p = name; ; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            if (tp < NAMELEN - 1) tok[nt][tp++] = c;
        } else {
            if (tp > 0) { tok[nt][tp] = 0; if (++nt >= maxtok) return nt; tp = 0; }
            if (c == 0) break;
        }
    }
    return nt;
}

static void class_add(int a, DictionaryWord* w) {
    if (a < 0 || a >= 32) return;
    for (int i = 0; i < attr_n[a]; i++) if (attr_nouns[a][i] == w) return;   // dedup
    if (attr_n[a] < CLASSCAP) attr_nouns[a][attr_n[a]++] = w;
}

static int specific(int a) { return a > 0 && a < 32 && attr_n[a] >= 1 && attr_n[a] <= MAX_CLASS; }

void build_typeahead_from_story(TrieNode* root, const unsigned char* story, unsigned int len) {
    S = story; SLEN = len;
    if (len < 0x40 || story[0] != 3) return;           // v3 only

    unsigned int dict_addr = rd16(0x08);
    unsigned int abbr = rd16(0x18);
    unsigned int obj_table = rd16(0x0a);
    unsigned int static_base = rd16(0x0e);

    // --- dictionary: words + flags + ids ---
    unsigned int p = dict_addr;
    int nsep = S[p]; p += 1 + nsep;
    int entry_len = S[p]; p += 1;
    int count = rd16(p); p += 2;
    if (count > MAXW) count = MAXW;
    d_count = 0;
    for (int k = 0; k < count; k++) {
        unsigned int off = p + (unsigned int) k * entry_len;
        char buf[12];
        dict_word(off, buf);
        if (!is_alpha_word(buf)) continue;
        strcpy(d_text[d_count], buf);
        d_flags[d_count] = S[off + 4];
        d_id[d_count] = S[off + 5];
        d_count++;
    }

    // --- objects: short-name + attribute bits ---
    unsigned int base = obj_table + 31 * 2;            // 31 two-byte property defaults
    unsigned int min_prop = 0xffffffff;
    o_count = 0;
    for (int k = 0; base + (unsigned int) k * 9 + 9 <= len && base + (unsigned int) k * 9 < min_prop; k++) {
        unsigned int e = base + (unsigned int) k * 9;
        unsigned int prop = rd16(e + 7);
        if (prop > 0 && prop < min_prop) min_prop = prop;
        if (o_count >= MAXOBJ) break;
        unsigned int bits = ((unsigned int) S[e] << 24) | ((unsigned int) S[e + 1] << 16)
                          | ((unsigned int) S[e + 2] << 8) | S[e + 3];
        o_attrs[o_count] = bits;
        o_name[o_count][0] = 0;
        if (prop > 0 && prop < len && S[prop] != 0)
            decode_at(prop + 1, abbr, 1, o_name[o_count], NAMELEN, 0);
        o_count++;
    }

    // --- full-word recovery: replace a 6-char dict form with a longer object
    //     name token that has the same first 6 characters ---
    char tok[8][NAMELEN];
    for (int o = 0; o < o_count; o++) {
        int nt = tokenize(o_name[o], tok, 8);
        for (int t = 0; t < nt; t++) {
            int tl = (int) strlen(tok[t]);
            if (tl <= 6) continue;
            for (int i = 0; i < d_count; i++) {
                if ((int) strlen(d_text[i]) == 6 && strncmp(d_text[i], tok[t], 6) == 0) {
                    strncpy(d_text[i], tok[t], 11); d_text[i][11] = 0;
                    break;
                }
            }
        }
    }

    // --- create words (full spellings) and insert into the trie ---
    for (int i = 0; i < d_count; i++) {
        if ((d_flags[i] & FL_DIR) && is_dir_abbrev(d_text[i])) { d_word[i] = NULL; continue; }
        if (d_flags[i] & FL_DIR) diag_full(d_text[i]);   // northe -> northeast
        WordType ty = TYPE_UNKNOWN;
        if (d_flags[i] & FL_NOUN) ty = TYPE_NOUN;
        else if (d_flags[i] & FL_VERB) ty = TYPE_VERB;
        else if (d_flags[i] & FL_DIR) ty = TYPE_DIRECTION;
        else if (d_flags[i] & FL_PREP) ty = TYPE_PREP;
        int wt = BASE_DEF;
        if (d_flags[i] & FL_DIR) wt = BASE_DIR;
        else if (d_flags[i] & FL_VERB) {
            wt = BASE_VERB;
            int cw = common_verb_weight(d_text[i]);
            if (cw > wt) wt = cw;               // lift the standard IF verbs
        }
        d_word[i] = create_word(d_text[i], ty, wt);
        insert_trie(root, d_word[i]);
    }

    // "reboot" is the Saturn port's global return-to-title command -- not in any
    // game's dictionary, so add it so every game can suggest/complete it.
    if (find_exact_word(root, "reboot") == NULL)
        insert_trie(root, create_word("reboot", TYPE_VERB, BASE_VERB));

    // --- canonical preposition word per id (prefer common spellings) ---
    for (int i = 0; i < 256; i++) prep_canon[i] = NULL;
    static int pref_rank[256];
    for (int i = 0; i < d_count; i++) {
        if (!(d_flags[i] & FL_PREP) || d_word[i] == NULL) continue;
        int id = d_id[i];
        int rank = 999;
        for (int r = 0; r < (int)(sizeof(PREP_PREF) / sizeof(PREP_PREF[0])); r++)
            if (strcmp(d_text[i], PREP_PREF[r]) == 0) { rank = r; break; }
        if (prep_canon[id] == NULL || rank < pref_rank[id]) {
            prep_canon[id] = d_word[i];
            pref_rank[id] = rank;
        }
    }

    // --- object attribute -> concrete nouns, and adjective -> head noun ---
    for (int a = 0; a < 32; a++) attr_n[a] = 0;
    for (int o = 0; o < o_count; o++) {
        int nt = tokenize(o_name[o], tok, 8);
        if (nt == 0) continue;
        DictionaryWord* head = find_exact_word(root, tok[nt - 1]);
        if (!head) continue;
        for (int a = 0; a < 32; a++)
            if (o_attrs[o] & (1u << (31 - a))) class_add(a, head);
        for (int t = 0; t < nt - 1; t++) {
            if (!(flags_of(tok[t]) & FL_ADJ)) continue;
            DictionaryWord* adj = find_exact_word(root, tok[t]);
            if (adj && adj != head) add_next_word(adj, head, W_ADJ_NOUN);
        }
    }

    // --- grammar transitions: verb->prep, verb->noun, prep->noun ---
    unsigned int prep_gov[256];
    for (int i = 0; i < 256; i++) prep_gov[i] = 0;
    for (int i = 0; i < d_count; i++) {
        if (!(d_flags[i] & FL_VERB) || d_word[i] == NULL) continue;
        unsigned int a = rd16(static_base + 2 * (255 - d_id[i]));
        if (a < static_base || a >= len) continue;
        int nrows = S[a];
        if (nrows < 1 || nrows > 12) continue;

        int preps[24], np = 0, dattrs[16], nd = 0;
        for (int e = 0; e < nrows; e++) {
            unsigned int r = a + 1 + (unsigned int) e * 8;
            if (r + 4 >= len) break;
            int p1 = S[r + 1], p2 = S[r + 2], o1 = S[r + 3], o2 = S[r + 4];
            int adds[2] = { p1, p2 };
            for (int q = 0; q < 2; q++) {
                int pp = adds[q];
                if (pp && prep_canon[pp]) {
                    int seen = 0;
                    for (int j = 0; j < np; j++) if (preps[j] == pp) { seen = 1; break; }
                    if (!seen && np < 24) preps[np++] = pp;
                }
            }
            if (o1) {
                int seen = 0;
                for (int j = 0; j < nd; j++) if (dattrs[j] == o1) { seen = 1; break; }
                if (!seen && nd < 16) dattrs[nd++] = o1;
            }
            if (p1 && o1 < 32) prep_gov[p1] |= (1u << o1);
            if (p2 && o2 < 32) prep_gov[p2] |= (1u << o2);
        }
        for (int j = 0; j < np; j++)
            add_next_word(d_word[i], prep_canon[preps[j]], W_VERB_PREP);
        for (int j = 0; j < nd; j++)
            if (specific(dattrs[j]))
                for (int m = 0; m < attr_n[dattrs[j]]; m++)
                    add_next_word(d_word[i], attr_nouns[dattrs[j]][m], W_VERB_NOUN);
    }

    // preposition -> the object class it governs (global across verbs)
    for (int id = 0; id < 256; id++) {
        if (!prep_canon[id] || !prep_gov[id]) continue;
        for (int a = 0; a < 32; a++) {
            if (!(prep_gov[id] & (1u << a)) || !specific(a)) continue;
            for (int m = 0; m < attr_n[a]; m++)
                add_next_word(prep_canon[id], attr_nouns[a][m], W_PREP_NOUN);
        }
    }
}
