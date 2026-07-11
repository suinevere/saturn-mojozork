// Zork I typeahead vocabulary and context transitions.
//
// Verbs, nouns, directions and prepositions from Zork I, plus the common
// word-to-word transitions that drive context-aware prediction (e.g. after
// "take" suggest carryable items; after "go" suggest directions; after "kill"
// suggest a monster; after "with" suggest a weapon). Sourced from the Zork I
// command list and full playthrough transcripts.
//
//   base_weight       -- ranks bare-prefix completions (higher wins for a
//                        shared prefix). Movement is the most-typed command in
//                        Zork, so the cardinal directions sit at the top.
//   transition_weight -- ranks "word B follows word A" suggestions; the canonical
//                        pairing from the transcripts gets the highest value.

#include "typeahead_zork.h"

// Create a word and insert it into the trie in one step.
static DictionaryWord* mk(TrieNode* root, const char* text, WordType type, int weight) {
    DictionaryWord* w = create_word(text, type, weight);
    insert_trie(root, w);
    return w;
}

// Link one source word to every target in a list with the same weight.
static void link_all(DictionaryWord* src, DictionaryWord* const* tgts, int n, int w) {
    for (int i = 0; i < n; i++) add_next_word(src, tgts[i], w);
}

// Link every source to every target (Cartesian product) at one weight.
static void link_cross(DictionaryWord* const* srcs, int ns,
                       DictionaryWord* const* tgts, int nt, int w) {
    for (int i = 0; i < ns; i++) link_all(srcs[i], tgts, nt, w);
}

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void build_zork_typeahead(TrieNode* root) {
    // ---- Directions (weighted high: movement is the most common command) ----
    DictionaryWord* d_north = mk(root, "north", TYPE_DIRECTION, 100);
    DictionaryWord* d_south = mk(root, "south", TYPE_DIRECTION, 100);
    DictionaryWord* d_east  = mk(root, "east",  TYPE_DIRECTION, 100);
    DictionaryWord* d_west  = mk(root, "west",  TYPE_DIRECTION, 100);
    DictionaryWord* d_ne    = mk(root, "northeast", TYPE_DIRECTION, 84);
    DictionaryWord* d_nw    = mk(root, "northwest", TYPE_DIRECTION, 84);
    DictionaryWord* d_se    = mk(root, "southeast", TYPE_DIRECTION, 84);
    DictionaryWord* d_sw    = mk(root, "southwest", TYPE_DIRECTION, 84);
    DictionaryWord* d_up    = mk(root, "up",   TYPE_DIRECTION, 95);
    DictionaryWord* d_down  = mk(root, "down", TYPE_DIRECTION, 95);
    DictionaryWord* d_in    = mk(root, "in",   TYPE_DIRECTION, 70);
    DictionaryWord* d_out   = mk(root, "out",  TYPE_DIRECTION, 70);
    DictionaryWord* dirs[] = { d_north, d_south, d_east, d_west,
                               d_ne, d_nw, d_se, d_sw, d_up, d_down, d_in, d_out };

    // ---- Prepositions / particles ----
    DictionaryWord* p_with   = mk(root, "with",   TYPE_PREP, 40);
    DictionaryWord* p_on      = mk(root, "on",     TYPE_PREP, 45);
    DictionaryWord* p_off     = mk(root, "off",    TYPE_PREP, 42);
    DictionaryWord* p_to      = mk(root, "to",     TYPE_PREP, 40);
    DictionaryWord* p_at      = mk(root, "at",     TYPE_PREP, 38);
    DictionaryWord* p_from    = mk(root, "from",   TYPE_PREP, 38);
    DictionaryWord* p_inside  = mk(root, "inside", TYPE_PREP, 40);
    DictionaryWord* p_into    = mk(root, "into",   TYPE_PREP, 38);
    DictionaryWord* p_under   = mk(root, "under",  TYPE_PREP, 30);
    DictionaryWord* p_of      = mk(root, "of",     TYPE_PREP, 25);

    // ---- Verbs ----
    DictionaryWord* v_take   = mk(root, "take",   TYPE_VERB, 98);
    DictionaryWord* v_get    = mk(root, "get",    TYPE_VERB, 74);
    DictionaryWord* v_grab   = mk(root, "grab",   TYPE_VERB, 60);
    DictionaryWord* v_pick   = mk(root, "pick",   TYPE_VERB, 52);
    DictionaryWord* v_drop   = mk(root, "drop",   TYPE_VERB, 90);
    DictionaryWord* v_open   = mk(root, "open",   TYPE_VERB, 100);
    DictionaryWord* v_close  = mk(root, "close",  TYPE_VERB, 62);
    DictionaryWord* v_read   = mk(root, "read",   TYPE_VERB, 86);
    DictionaryWord* v_put    = mk(root, "put",    TYPE_VERB, 94);
    DictionaryWord* v_turn   = mk(root, "turn",   TYPE_VERB, 80);
    DictionaryWord* v_push   = mk(root, "push",   TYPE_VERB, 70);
    DictionaryWord* v_pull   = mk(root, "pull",   TYPE_VERB, 60);
    DictionaryWord* v_move   = mk(root, "move",   TYPE_VERB, 72);
    DictionaryWord* v_kill   = mk(root, "kill",   TYPE_VERB, 85);
    DictionaryWord* v_attack = mk(root, "attack", TYPE_VERB, 66);
    DictionaryWord* v_go     = mk(root, "go",     TYPE_VERB, 96);
    DictionaryWord* v_walk   = mk(root, "walk",   TYPE_VERB, 50);
    DictionaryWord* v_enter  = mk(root, "enter",  TYPE_VERB, 82);
    DictionaryWord* v_exit   = mk(root, "exit",   TYPE_VERB, 48);
    DictionaryWord* v_climb  = mk(root, "climb",  TYPE_VERB, 76);
    DictionaryWord* v_look   = mk(root, "look",   TYPE_VERB, 92);
    DictionaryWord* v_examine= mk(root, "examine",TYPE_VERB, 70);
    DictionaryWord* v_search = mk(root, "search", TYPE_VERB, 46);
    DictionaryWord* v_inv    = mk(root, "inventory", TYPE_VERB, 90);
    DictionaryWord* v_give   = mk(root, "give",   TYPE_VERB, 75);
    DictionaryWord* v_tie    = mk(root, "tie",    TYPE_VERB, 65);
    DictionaryWord* v_untie  = mk(root, "untie",  TYPE_VERB, 40);
    DictionaryWord* v_unlock = mk(root, "unlock", TYPE_VERB, 64);
    DictionaryWord* v_lock   = mk(root, "lock",   TYPE_VERB, 44);
    DictionaryWord* v_light  = mk(root, "light",  TYPE_VERB, 78);
    DictionaryWord* v_burn   = mk(root, "burn",   TYPE_VERB, 42);
    DictionaryWord* v_ring   = mk(root, "ring",   TYPE_VERB, 62);
    DictionaryWord* v_wave   = mk(root, "wave",   TYPE_VERB, 60);
    DictionaryWord* v_rub    = mk(root, "rub",    TYPE_VERB, 58);
    DictionaryWord* v_touch  = mk(root, "touch",  TYPE_VERB, 44);
    DictionaryWord* v_dig    = mk(root, "dig",    TYPE_VERB, 66);
    DictionaryWord* v_pray   = mk(root, "pray",   TYPE_VERB, 63);
    DictionaryWord* v_wait   = mk(root, "wait",   TYPE_VERB, 84);
    DictionaryWord* v_launch = mk(root, "launch", TYPE_VERB, 55);
    DictionaryWord* v_board  = mk(root, "board",  TYPE_VERB, 46);
    DictionaryWord* v_inflate= mk(root, "inflate",TYPE_VERB, 54);
    DictionaryWord* v_leave  = mk(root, "leave",  TYPE_VERB, 58);
    DictionaryWord* v_cross  = mk(root, "cross",  TYPE_VERB, 57);
    DictionaryWord* v_wind   = mk(root, "wind",   TYPE_VERB, 59);
    DictionaryWord* v_throw  = mk(root, "throw",  TYPE_VERB, 68);
    DictionaryWord* v_break  = mk(root, "break",  TYPE_VERB, 56);
    DictionaryWord* v_cut    = mk(root, "cut",    TYPE_VERB, 55);
    DictionaryWord* v_eat    = mk(root, "eat",    TYPE_VERB, 60);
    DictionaryWord* v_drink  = mk(root, "drink",  TYPE_VERB, 58);
    DictionaryWord* v_smell  = mk(root, "smell",  TYPE_VERB, 50);
    DictionaryWord* v_listen = mk(root, "listen", TYPE_VERB, 48);
    DictionaryWord* v_pour   = mk(root, "pour",   TYPE_VERB, 50);
    DictionaryWord* v_fill   = mk(root, "fill",   TYPE_VERB, 48);
    DictionaryWord* v_lower  = mk(root, "lower",  TYPE_VERB, 60);
    DictionaryWord* v_raise  = mk(root, "raise",  TYPE_VERB, 60);
    DictionaryWord* v_knock  = mk(root, "knock",  TYPE_VERB, 44);
    DictionaryWord* v_swing  = mk(root, "swing",  TYPE_VERB, 45);
    DictionaryWord* v_jump   = mk(root, "jump",   TYPE_VERB, 45);
    DictionaryWord* v_echo   = mk(root, "echo",   TYPE_VERB, 40);
    DictionaryWord* v_count  = mk(root, "count",  TYPE_VERB, 35);

    // Meta / out-of-world commands (completion only, no transitions).
    mk(root, "save",       TYPE_VERB, 70);
    mk(root, "restore",    TYPE_VERB, 55);
    mk(root, "restart",    TYPE_VERB, 50);
    mk(root, "quit",       TYPE_VERB, 52);
    mk(root, "score",      TYPE_VERB, 56);
    mk(root, "diagnose",   TYPE_VERB, 45);
    mk(root, "verbose",    TYPE_VERB, 40);
    mk(root, "brief",      TYPE_VERB, 40);
    mk(root, "superbrief", TYPE_VERB, 38);
    mk(root, "version",    TYPE_VERB, 35);
    mk(root, "again",      TYPE_VERB, 42);
    mk(root, "hello",      TYPE_VERB, 30);
    mk(root, "yell",       TYPE_VERB, 30);
    mk(root, "wear",       TYPE_VERB, 44);
    mk(root, "remove",     TYPE_VERB, 44);
    mk(root, "insert",     TYPE_VERB, 44);

    // ---- Nouns: carryable items ----
    DictionaryWord* n_lamp    = mk(root, "lamp",    TYPE_NOUN, 72);
    DictionaryWord* n_lantern = mk(root, "lantern", TYPE_NOUN, 40);
    DictionaryWord* n_sword   = mk(root, "sword",   TYPE_NOUN, 66);
    DictionaryWord* n_knife   = mk(root, "knife",   TYPE_NOUN, 60);
    DictionaryWord* n_rope    = mk(root, "rope",    TYPE_NOUN, 55);
    DictionaryWord* n_painting= mk(root, "painting",TYPE_NOUN, 50);
    DictionaryWord* n_coffin  = mk(root, "coffin",  TYPE_NOUN, 50);
    DictionaryWord* n_sceptre = mk(root, "sceptre", TYPE_NOUN, 48);
    DictionaryWord* n_gold    = mk(root, "gold",    TYPE_NOUN, 48);
    DictionaryWord* n_garlic  = mk(root, "garlic",  TYPE_NOUN, 46);
    DictionaryWord* n_matchbk = mk(root, "matchbook", TYPE_NOUN, 30);
    DictionaryWord* n_match   = mk(root, "match",   TYPE_NOUN, 52);
    DictionaryWord* n_wrench  = mk(root, "wrench",  TYPE_NOUN, 48);
    DictionaryWord* n_screw   = mk(root, "screwdriver", TYPE_NOUN, 48);
    DictionaryWord* n_bell    = mk(root, "bell",    TYPE_NOUN, 46);
    DictionaryWord* n_candles = mk(root, "candles", TYPE_NOUN, 46);
    DictionaryWord* n_book    = mk(root, "book",    TYPE_NOUN, 50);
    DictionaryWord* n_skull   = mk(root, "skull",   TYPE_NOUN, 46);
    DictionaryWord* n_torch   = mk(root, "torch",   TYPE_NOUN, 52);
    DictionaryWord* n_coal    = mk(root, "coal",    TYPE_NOUN, 46);
    DictionaryWord* n_diamond = mk(root, "diamond", TYPE_NOUN, 48);
    DictionaryWord* n_jade    = mk(root, "jade",    TYPE_NOUN, 46);
    DictionaryWord* n_buoy    = mk(root, "buoy",    TYPE_NOUN, 44);
    DictionaryWord* n_pump    = mk(root, "pump",    TYPE_NOUN, 44);
    DictionaryWord* n_shovel  = mk(root, "shovel",  TYPE_NOUN, 46);
    DictionaryWord* n_scarab  = mk(root, "scarab",  TYPE_NOUN, 44);
    DictionaryWord* n_emerald = mk(root, "emerald", TYPE_NOUN, 46);
    DictionaryWord* n_egg     = mk(root, "egg",     TYPE_NOUN, 50);
    DictionaryWord* n_stiletto= mk(root, "stiletto",TYPE_NOUN, 42);
    DictionaryWord* n_chalice = mk(root, "chalice", TYPE_NOUN, 46);
    DictionaryWord* n_canary  = mk(root, "canary",  TYPE_NOUN, 44);
    DictionaryWord* n_bauble  = mk(root, "bauble",  TYPE_NOUN, 44);
    DictionaryWord* n_bracelet= mk(root, "bracelet",TYPE_NOUN, 44);
    DictionaryWord* n_bar     = mk(root, "bar",     TYPE_NOUN, 44);
    DictionaryWord* n_leaflet = mk(root, "leaflet", TYPE_NOUN, 44);
    DictionaryWord* n_axe     = mk(root, "axe",     TYPE_NOUN, 46);
    DictionaryWord* n_key     = mk(root, "key",     TYPE_NOUN, 46);
    DictionaryWord* n_coins   = mk(root, "coins",   TYPE_NOUN, 44);
    DictionaryWord* n_trident = mk(root, "trident", TYPE_NOUN, 44);
    DictionaryWord* n_trunk   = mk(root, "trunk",   TYPE_NOUN, 44);
    DictionaryWord* n_jewels  = mk(root, "jewels",  TYPE_NOUN, 44);
    DictionaryWord* n_bottle  = mk(root, "bottle",  TYPE_NOUN, 44);
    DictionaryWord* n_water   = mk(root, "water",   TYPE_NOUN, 42);
    DictionaryWord* n_wand    = mk(root, "wand",    TYPE_NOUN, 40);
    DictionaryWord* n_all     = mk(root, "all",     TYPE_NOUN, 70);

    // ---- Nouns: containers / fixtures / scenery (openable or targeted) ----
    DictionaryWord* n_mailbox = mk(root, "mailbox", TYPE_NOUN, 54);
    DictionaryWord* n_window  = mk(root, "window",  TYPE_NOUN, 50);
    DictionaryWord* n_door    = mk(root, "door",    TYPE_NOUN, 52);
    DictionaryWord* n_trapdoor= mk(root, "trapdoor",TYPE_NOUN, 30);
    DictionaryWord* n_trap    = mk(root, "trap",    TYPE_NOUN, 44);
    DictionaryWord* n_case    = mk(root, "case",    TYPE_NOUN, 52);
    DictionaryWord* n_bag     = mk(root, "bag",     TYPE_NOUN, 46);
    DictionaryWord* n_sack    = mk(root, "sack",    TYPE_NOUN, 44);
    DictionaryWord* n_grate   = mk(root, "grate",   TYPE_NOUN, 44);
    DictionaryWord* n_lid     = mk(root, "lid",     TYPE_NOUN, 44);
    DictionaryWord* n_machine = mk(root, "machine", TYPE_NOUN, 46);
    DictionaryWord* n_basket  = mk(root, "basket",  TYPE_NOUN, 46);
    DictionaryWord* n_boat    = mk(root, "boat",    TYPE_NOUN, 48);
    DictionaryWord* n_nest    = mk(root, "nest",    TYPE_NOUN, 42);
    DictionaryWord* n_house   = mk(root, "house",   TYPE_NOUN, 50);
    DictionaryWord* n_rug     = mk(root, "rug",     TYPE_NOUN, 46);
    DictionaryWord* n_button  = mk(root, "button",  TYPE_NOUN, 46);
    DictionaryWord* n_bolt    = mk(root, "bolt",    TYPE_NOUN, 44);
    DictionaryWord* n_switch  = mk(root, "switch",  TYPE_NOUN, 44);
    DictionaryWord* n_mirror  = mk(root, "mirror",  TYPE_NOUN, 46);
    DictionaryWord* n_tree    = mk(root, "tree",    TYPE_NOUN, 48);
    DictionaryWord* n_rainbow = mk(root, "rainbow", TYPE_NOUN, 46);
    DictionaryWord* n_railing = mk(root, "railing", TYPE_NOUN, 42);
    DictionaryWord* n_sand    = mk(root, "sand",    TYPE_NOUN, 46);
    DictionaryWord* n_plastic = mk(root, "plastic", TYPE_NOUN, 42);

    // ---- Nouns: monsters ----
    DictionaryWord* n_troll   = mk(root, "troll",   TYPE_NOUN, 54);
    DictionaryWord* n_thief   = mk(root, "thief",   TYPE_NOUN, 52);
    DictionaryWord* n_cyclops = mk(root, "cyclops", TYPE_NOUN, 48);
    DictionaryWord* n_grue    = mk(root, "grue",    TYPE_NOUN, 40);
    DictionaryWord* n_bat     = mk(root, "bat",     TYPE_NOUN, 40);

    // Extra scenery/adjective vocabulary -- completion only (no transitions).
    static const char* const extras[] = {
        // adjectives Zork's parser accepts
        "yellow", "red", "blue", "brown", "green", "brass", "elvish", "nasty",
        "rusty", "bloody", "wooden", "glass", "golden", "silver", "crystal",
        "platinum", "jeweled", "sapphire", "ancient", "trophy", "magic",
        "prayer", "leather", "metal", "granite", "marble", "large", "small",
        // scenery and other nouns seen underground
        "table", "staircase", "stairway", "chimney", "passage", "chasm", "dam",
        "panel", "gates", "pedestal", "altar", "gate", "spirits", "songbird",
        "lunch", "food", "label", "valve", "chain", "shaft", "ledge", "cliff",
        "beach", "river", "falls", "forest", "path", "wall", "hole", "ladder",
        "timber", "skeleton", "tube", "putty", "guidebook", "chest", "tools",
        "pot", "leaves",
    };
    for (int i = 0; i < COUNT(extras); i++) mk(root, extras[i], TYPE_NOUN, 30);

    // ================= Context transitions =================

    // Movement: go/walk -> any direction (cardinals suggested first).
    DictionaryWord* v_move_cmds[] = { v_go, v_walk };
    link_cross(v_move_cmds, COUNT(v_move_cmds), dirs, COUNT(dirs), 70);
    DictionaryWord* cardinals[] = { d_north, d_south, d_east, d_west, d_up, d_down };
    link_all(v_go, cardinals, COUNT(cardinals), 90);   // boost the common ones

    // climb -> up/down/tree; enter -> house/window/boat.
    DictionaryWord* climb_t[] = { d_up, d_down, n_tree };
    link_all(v_climb, climb_t, COUNT(climb_t), 80);
    DictionaryWord* enter_t[] = { n_house, n_window, n_boat };
    link_all(v_enter, enter_t, COUNT(enter_t), 78);

    // Carryable items: shared by take/get/grab/pick, drop, put, examine, throw.
    DictionaryWord* items[] = {
        n_lamp, n_lantern, n_sword, n_knife, n_rope, n_painting, n_coffin,
        n_sceptre, n_gold, n_garlic, n_matchbk, n_match, n_wrench, n_screw,
        n_bell, n_candles, n_book, n_skull, n_torch, n_coal, n_diamond, n_jade,
        n_buoy, n_pump, n_shovel, n_scarab, n_emerald, n_egg, n_stiletto,
        n_chalice, n_canary, n_bauble, n_bracelet, n_bar, n_leaflet, n_axe,
        n_key, n_coins, n_trident, n_trunk, n_jewels, n_bottle, n_water, n_all,
    };
    DictionaryWord* take_cmds[] = { v_take, v_get, v_grab, v_pick };
    link_cross(take_cmds, COUNT(take_cmds), items, COUNT(items), 55);
    link_all(v_drop, items, COUNT(items), 52);
    link_all(v_put,  items, COUNT(items), 50);
    DictionaryWord* look_cmds[] = { v_examine, v_look, v_search, v_throw, v_give };
    link_cross(look_cmds, COUNT(look_cmds), items, COUNT(items), 38);
    // A few iconic take pairings ranked above the rest.
    add_next_word(v_take, n_lamp,  92);
    add_next_word(v_take, n_sword, 88);
    add_next_word(v_take, n_egg,   80);

    // Containers/fixtures: open/close, and targets of in/inside/into/from.
    DictionaryWord* containers[] = {
        n_mailbox, n_case, n_coffin, n_bag, n_sack, n_trapdoor, n_trap, n_door,
        n_window, n_grate, n_lid, n_machine, n_basket, n_bottle, n_buoy, n_egg,
        n_boat, n_nest, n_trunk,
    };
    link_all(v_open,  containers, COUNT(containers), 68);
    DictionaryWord* close_t[] = { n_lid, n_door, n_case, n_trapdoor, n_window, n_grate };
    link_all(v_close, close_t, COUNT(close_t), 62);
    add_next_word(v_open, n_mailbox, 90);   // the first thing every player opens

    // read -> readables.
    DictionaryWord* read_t[] = { n_leaflet, n_book, n_matchbk };
    link_all(v_read, read_t, COUNT(read_t), 72);
    add_next_word(v_read, n_leaflet, 88);

    // move -> rug (and other heavy scenery).
    add_next_word(v_move, n_rug, 88);

    // turn -> on/off/bolt/switch; then on/off -> the light sources.
    add_next_word(v_turn, p_on,  85);
    add_next_word(v_turn, p_off, 80);
    add_next_word(v_turn, n_bolt,   58);
    add_next_word(v_turn, n_switch, 58);
    DictionaryWord* lights[] = { n_lamp, n_lantern, n_torch };
    link_all(p_on,  lights, COUNT(lights), 70);
    link_all(p_off, lights, COUNT(lights), 70);

    // push -> button (colored buttons live in `extras`, completion only).
    add_next_word(v_push, n_button, 80);

    // Combat: kill/attack -> monster.
    DictionaryWord* monsters[] = { n_troll, n_thief, n_cyclops, n_grue, n_bat };
    DictionaryWord* fight_cmds[] = { v_kill, v_attack };
    link_cross(fight_cmds, COUNT(fight_cmds), monsters, COUNT(monsters), 78);
    add_next_word(v_kill, n_troll, 86);
    add_next_word(v_kill, n_thief, 84);

    // Single-object action verbs.
    add_next_word(v_give,    n_egg,     82);
    add_next_word(v_tie,     n_rope,    90);
    add_next_word(v_light,   n_match,   82);
    add_next_word(v_light,   n_candles, 80);
    add_next_word(v_burn,    n_candles, 60);
    add_next_word(v_ring,    n_bell,    90);
    add_next_word(v_wave,    n_sceptre, 88);
    add_next_word(v_wave,    n_wand,    60);
    add_next_word(v_rub,     n_mirror,  86);
    add_next_word(v_rub,     n_lamp,    50);
    add_next_word(v_unlock,  n_grate,   82);
    add_next_word(v_lock,    n_grate,   60);
    add_next_word(v_dig,     n_sand,    86);
    add_next_word(v_inflate, n_plastic, 84);
    add_next_word(v_inflate, n_boat,    60);
    add_next_word(v_cross,   n_rainbow, 90);
    add_next_word(v_wind,    d_up,      82);   // "wind up canary"
    add_next_word(v_wind,    n_canary,  70);
    add_next_word(v_eat,     n_garlic,  70);
    add_next_word(v_eat,     n_water,   30);
    add_next_word(v_drink,   n_water,   80);
    add_next_word(v_launch, n_boat, 60);
    add_next_word(v_leave, n_boat, 78);
    add_next_word(v_board, n_boat, 78);
    add_next_word(v_lower, n_basket, 70);
    add_next_word(v_raise, n_basket, 70);

    // Prepositions -> their usual objects.
    DictionaryWord* weapons[] = {
        n_sword, n_knife, n_axe, n_stiletto, n_wrench, n_screw, n_torch,
        n_rope, n_pump, n_key, n_match, n_shovel,
    };
    link_all(p_with, weapons, COUNT(weapons), 55);
    add_next_word(p_with, n_sword, 80);
    add_next_word(p_with, n_wrench, 70);   // turn bolt with wrench
    add_next_word(p_with, n_screw, 70);    // turn switch with screwdriver
    add_next_word(p_with, n_pump, 65);     // inflate plastic with pump

    DictionaryWord* in_targets[] = {
        n_case, n_basket, n_machine, n_boat, n_coffin, n_bag, n_egg, n_bottle,
        n_trunk, n_buoy, n_nest,
    };
    // "in" shares the direction token d_in; give it the container transitions too.
    link_all(d_in,     in_targets, COUNT(in_targets), 55);
    link_all(p_inside, in_targets, COUNT(in_targets), 55);
    link_all(p_into,   in_targets, COUNT(in_targets), 50);
    add_next_word(d_in, n_case, 80);       // put <treasure> in case

    DictionaryWord* to_targets[] = { n_thief, n_railing, n_troll };
    link_all(p_to, to_targets, COUNT(to_targets), 55);
    add_next_word(p_to, n_thief,   72);    // give egg to thief
    add_next_word(p_to, n_railing, 70);    // tie rope to railing

    DictionaryWord* at_targets[] = { n_troll, n_thief, n_cyclops };
    link_all(p_at, at_targets, COUNT(at_targets), 55);

    DictionaryWord* from_targets[] = { n_egg, n_case, n_coffin, n_bag, n_nest, n_buoy };
    link_all(p_from, from_targets, COUNT(from_targets), 50);

    // Verbs below are recognised for completion but take no context transitions.
    (void) p_under; (void) p_of; (void) v_untie; (void) v_exit; (void) v_pull;
    (void) v_pour; (void) v_fill; (void) v_smell; (void) v_listen; (void) v_touch;
    (void) v_knock; (void) v_swing; (void) v_jump; (void) v_echo; (void) v_count;
    (void) v_pray; (void) v_wait; (void) v_inv; (void) v_break; (void) v_cut;
}
