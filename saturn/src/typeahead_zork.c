// GENERATED FILE -- do not edit by hand.
// Produced by tools/typeahead/gen_typeahead.py from:
//   story:      saturn/cd/data/Z3/ZORK1.Z3
//   walkthrough:tools/typeahead/zork1_walkthrough.txt
//   words: 687   transitions: 149
//
// Vocabulary is the story's own parser dictionary; base weights and context
// transitions come from a winning command script (frequency and word bigrams),
// so completion favours the winning move.

#include "typeahead_zork.h"

typedef struct { const char* t; short wt; } ZWord;
typedef struct { short a, b, wt; } ZLink;

static const ZWord ZWORDS[] = {
    {"a",30}, {"across",30}, {"activa",30}, {"advent",30}, {"advert",30}, {"again",30},
    {"air",30}, {"all",43}, {"altar",30}, {"an",30}, {"ancien",30}, {"and",30},
    {"answer",30}, {"antiqu",30}, {"apply",30}, {"around",30}, {"art",30}, {"ask",30},
    {"at",30}, {"attach",30}, {"attack",30}, {"aviato",30}, {"awake",30}, {"away",30},
    {"ax",30}, {"axe",30}, {"back",30}, {"bag",41}, {"banish",30}, {"bar",42},
    {"bare",30}, {"barf",30}, {"barrow",30}, {"basket",44}, {"bat",30}, {"bathe",30},
    {"bauble",42}, {"beauti",30}, {"beetle",30}, {"begone",30}, {"behind",30}, {"bell",42},
    {"below",30}, {"beneat",30}, {"bird",30}, {"birds",30}, {"bite",30}, {"black",30},
    {"blade",30}, {"blast",30}, {"blessi",30}, {"block",30}, {"bloody",30}, {"blow",30},
    {"blue",30}, {"board",30}, {"boarde",30}, {"boards",30}, {"boat",42}, {"bodies",30},
    {"body",30}, {"bolt",41}, {"bones",30}, {"book",43}, {"bookle",30}, {"books",30},
    {"bottle",30}, {"box",30}, {"bracelet",41}, {"branch",30}, {"brandi",30}, {"brass",30},
    {"break",30}, {"breath",30}, {"brief",30}, {"broken",30}, {"brown",30}, {"brush",30},
    {"bubble",30}, {"bug",30}, {"buoy",43}, {"burn",30}, {"burned",30}, {"burnin",30},
    {"but",30}, {"button",41}, {"cage",30}, {"canary",43}, {"candles",43}, {"canvas",30},
    {"carpet",30}, {"carry",30}, {"carved",30}, {"case",52}, {"casket",30}, {"cast",30},
    {"catch",30}, {"chain",30}, {"chalice",42}, {"chant",30}, {"chase",30}, {"chasm",30},
    {"chest",30}, {"chests",30}, {"chimney",41}, {"chomp",30}, {"chuck",30}, {"chute",30},
    {"clean",30}, {"clear",30}, {"cliff",30}, {"cliffs",30}, {"climb",43}, {"clockw",30},
    {"close",41}, {"clove",30}, {"coal",43}, {"coffin",45}, {"coil",30}, {"coins",30},
    {"coloni",30}, {"come",30}, {"comman",30}, {"consum",30}, {"contai",30}, {"contro",30},
    {"count",30}, {"cover",30}, {"crack",30}, {"crawlw",30}, {"cretin",30}, {"cross",41},
    {"crysta",30}, {"cup",30}, {"curse",30}, {"cut",30}, {"cyclop",30}, {"d",30},
    {"dam",30}, {"damage",30}, {"damn",30}, {"dark",30}, {"dead",30}, {"deflat",30},
    {"derang",30}, {"descri",30}, {"destro",30}, {"diagno",30}, {"diamond",42}, {"dig",44},
    {"dinner",30}, {"dirt",30}, {"disemb",30}, {"disenc",30}, {"dispat",30}, {"dive",30},
    {"dome",30}, {"donate",30}, {"door",43}, {"douse",30}, {"down",68}, {"drink",30},
    {"drip",30}, {"drive",30}, {"driver",30}, {"drop",54}, {"dryer",30}, {"dumbwa",30},
    {"dusty",30}, {"e",30}, {"east",79}, {"eat",30}, {"echo",41}, {"egg",44},
    {"egypti",30}, {"elonga",30}, {"elvish",30}, {"emerald",41}, {"enamel",30}, {"enchan",30},
    {"encrus",30}, {"engrav",30}, {"enormo",30}, {"enter",45}, {"evil",30}, {"examin",30},
    {"except",30}, {"exit",30}, {"exorci",30}, {"exquis",30}, {"exting",30}, {"eye",30},
    {"fall",30}, {"fantas",30}, {"fasten",30}, {"fear",30}, {"feeble",30}, {"feed",30},
    {"feel",30}, {"fence",30}, {"fermen",30}, {"fiends",30}, {"fierce",30}, {"fight",30},
    {"figuri",30}, {"filch",30}, {"fill",30}, {"find",30}, {"fine",30}, {"finepr",30},
    {"firepr",30}, {"fix",30}, {"flamin",30}, {"flathe",30}, {"flip",30}, {"float",30},
    {"floor",30}, {"fluore",30}, {"follow",30}, {"foobar",30}, {"food",30}, {"footpa",30},
    {"for",30}, {"forbid",30}, {"force",30}, {"ford",30}, {"forest",30}, {"fork",30},
    {"free",30}, {"freeze",30}, {"frigid",30}, {"froboz",30}, {"from",41}, {"front",30},
    {"frotz",30}, {"fry",30}, {"fuck",30}, {"fudge",30}, {"fumble",30}, {"g",30},
    {"garlic",44}, {"gas",30}, {"gate",30}, {"gates",30}, {"gaze",30}, {"get",30},
    {"ghosts",30}, {"giant",30}, {"give",41}, {"glamdr",30}, {"glass",30}, {"glue",30},
    {"go",100}, {"gold",42}, {"golden",30}, {"gothic",30}, {"grab",30}, {"graces",30},
    {"granit",30}, {"grate",42}, {"gratin",30}, {"grease",30}, {"green",30}, {"ground",30},
    {"group",30}, {"grue",30}, {"guide",30}, {"guideb",30}, {"gunk",30}, {"hand",30},
    {"hands",30}, {"hatch",30}, {"head",30}, {"heap",30}, {"hello",30}, {"hemloc",30},
    {"hemp",30}, {"her",30}, {"here",30}, {"hi",30}, {"hide",30}, {"him",30},
    {"hit",30}, {"hold",30}, {"hop",30}, {"hot",30}, {"house",45}, {"huge",30},
    {"hungry",30}, {"hurl",30}, {"hurt",30}, {"i",30}, {"ignite",30}, {"imbibe",30},
    {"impass",30}, {"in",55}, {"incant",30}, {"incine",30}, {"inflate",41}, {"injure",30},
    {"inscri",30}, {"insert",30}, {"inside",42}, {"intnum",30}, {"into",30}, {"inventory",41},
    {"invisi",30}, {"is",30}, {"it",30}, {"ivory",30}, {"jade",41}, {"jewel",30},
    {"jewele",30}, {"jewels",30}, {"jump",30}, {"key",30}, {"kick",30}, {"kill",48},
    {"kiss",30}, {"kitche",30}, {"knife",45}, {"knives",30}, {"knock",30}, {"l",30},
    {"label",30}, {"ladder",30}, {"lake",30}, {"lamp",48}, {"land",30}, {"lanter",30},
    {"large",30}, {"launch",41}, {"leaf",30}, {"leaflet",42}, {"leak",30}, {"lean",30},
    {"leap",30}, {"leathe",30}, {"leave",41}, {"leaves",30}, {"ledge",30}, {"letter",30},
    {"lid",43}, {"lift",30}, {"light",42}, {"liquid",30}, {"liquif",30}, {"listen",30},
    {"lock",30}, {"long",30}, {"look",30}, {"lose",30}, {"lower",30}, {"lowere",30},
    {"lubric",30}, {"lunch",30}, {"lungs",30}, {"lurkin",30}, {"machine",41}, {"magic",30},
    {"mail",30}, {"mailbox",41}, {"make",30}, {"man",30}, {"mangle",30}, {"manual",30},
    {"map",30}, {"marble",30}, {"massiv",30}, {"match",42}, {"matchb",30}, {"matches",41},
    {"materi",30}, {"me",30}, {"melt",30}, {"metal",30}, {"mirror",41}, {"molest",30},
    {"monste",30}, {"mounta",30}, {"mouth",30}, {"move",41}, {"mumble",30}, {"murder",30},
    {"myself",30}, {"n",30}, {"nail",30}, {"nails",30}, {"narrow",30}, {"nasty",30},
    {"ne",30}, {"nest",30}, {"no",30}, {"north",75}, {"northeast",45}, {"northwest",44},
    {"nut",30}, {"nw",30}, {"odor",30}, {"odysse",30}, {"of",30}, {"off",42},
    {"offer",30}, {"oil",30}, {"old",30}, {"on",44}, {"one",30}, {"onto",30},
    {"open",52}, {"orcris",30}, {"orient",30}, {"out",30}, {"over",30}, {"overbo",30},
    {"own",30}, {"owners",30}, {"ozmoo",30}, {"page",30}, {"paint",30}, {"painting",42},
    {"pair",30}, {"panel",30}, {"paper",30}, {"parchm",30}, {"passag",30}, {"paste",30},
    {"pat",30}, {"patch",30}, {"path",30}, {"peal",30}, {"pedest",30}, {"pepper",30},
    {"person",30}, {"pet",30}, {"pick",30}, {"piece",30}, {"pierce",30}, {"pile",30},
    {"pines",30}, {"pipe",30}, {"place",30}, {"plastic",41}, {"platin",30}, {"play",30},
    {"plug",30}, {"plugh",30}, {"poke",30}, {"poseid",30}, {"pot",30}, {"pour",30},
    {"pray",41}, {"prayer",30}, {"press",30}, {"print",30}, {"procee",30}, {"pull",30},
    {"pump",42}, {"punctu",30}, {"pursue",30}, {"push",41}, {"put",56}, {"q",30},
    {"quanti",30}, {"quit",30}, {"raft",30}, {"rail",30}, {"railing",41}, {"rainbow",41},
    {"raise",30}, {"ramp",30}, {"range",30}, {"rap",30}, {"rape",30}, {"read",42},
    {"red",30}, {"reflec",30}, {"releas",30}, {"remain",30}, {"remove",30}, {"repair",30},
    {"repent",30}, {"reply",30}, {"restar",30}, {"restor",30}, {"ricket",30}, {"ring",41},
    {"river",30}, {"robber",30}, {"rocky",30}, {"roll",30}, {"rope",42}, {"rub",41},
    {"rug",41}, {"run",30}, {"rusty",30}, {"s",30}, {"sack",30}, {"sailor",30},
    {"sand",44}, {"sandwi",30}, {"sapphi",30}, {"save",30}, {"say",30}, {"scarab",41},
    {"scepte",30}, {"sceptre",43}, {"score",30}, {"scream",30}, {"screw",30}, {"screwdriver",46},
    {"script",30}, {"se",30}, {"search",30}, {"seawor",30}, {"secure",30}, {"see",30},
    {"seedy",30}, {"seek",30}, {"self",30}, {"send",30}, {"set",30}, {"shady",30},
    {"shake",30}, {"sharp",30}, {"sheer",30}, {"shit",30}, {"shout",30}, {"shovel",43},
    {"shut",30}, {"sigh",30}, {"silent",30}, {"silver",30}, {"sinist",30}, {"sit",30},
    {"skelet",30}, {"skim",30}, {"skip",30}, {"skull",43}, {"slag",30}, {"slay",30},
    {"slice",30}, {"slide",30}, {"small",30}, {"smash",30}, {"smell",30}, {"smelly",30},
    {"sniff",30}, {"solid",30}, {"song",30}, {"songbi",30}, {"south",73}, {"southeast",43},
    {"southwest",47}, {"spill",30}, {"spin",30}, {"spirit",30}, {"spray",30}, {"squeez",30},
    {"stab",30}, {"stairc",30}, {"stairs",30}, {"stairw",30}, {"stand",30}, {"stare",30},
    {"startl",30}, {"stay",30}, {"steep",30}, {"step",30}, {"steps",30}, {"stiletto",41},
    {"stone",30}, {"storm",30}, {"strang",30}, {"stream",30}, {"strike",30}, {"stuff",30},
    {"super",30}, {"superb",30}, {"surpri",30}, {"surrou",30}, {"suspic",30}, {"sw",30},
    {"swallo",30}, {"swim",30}, {"swing",30}, {"switch",41}, {"sword",47}, {"table",30},
    {"take",80}, {"talk",30}, {"tan",30}, {"taste",30}, {"taunt",30}, {"teeth",30},
    {"tell",30}, {"temple",30}, {"the",30}, {"them",30}, {"then",30}, {"thief",44},
    {"thiefs",30}, {"throug",30}, {"throw",30}, {"thru",30}, {"thrust",30}, {"tie",41},
    {"timber",30}, {"to",42}, {"tomb",30}, {"tool",30}, {"toolch",30}, {"tools",30},
    {"tooth",30}, {"torch",43}, {"toss",30}, {"touch",30}, {"tour",30}, {"trail",30},
    {"trap",43}, {"trapdo",30}, {"treasu",30}, {"tree",42}, {"trees",30}, {"triden",30},
    {"troll",45}, {"trophy",30}, {"trunk",30}, {"tube",30}, {"tug",30}, {"turn",48},
    {"twisti",30}, {"u",30}, {"ulysses",41}, {"unatta",30}, {"under",30}, {"undern",30},
    {"unfast",30}, {"unhook",30}, {"unlock",41}, {"unrust",30}, {"unscri",30}, {"untie",30},
    {"up",63}, {"useles",30}, {"using",30}, {"valve",30}, {"vampir",30}, {"verbos",30},
    {"versio",30}, {"viciou",30}, {"viscou",30}, {"vitreo",30}, {"w",30}, {"wade",30},
    {"wait",43}, {"wake",30}, {"walk",30}, {"wall",30}, {"walls",30}, {"water",30},
    {"wave",41}, {"wear",30}, {"west",59}, {"what",30}, {"whats",30}, {"where",30},
    {"white",30}, {"win",30}, {"wind",41}, {"windin",30}, {"window",41}, {"winnag",30},
    {"wish",30}, {"with",52}, {"wooden",30}, {"wrench",43}, {"writin",30}, {"xyzzy",30},
    {"y",30}, {"yank",30}, {"yell",30}, {"yellow",41}, {"yes",30}, {"z",30},
    {"zork",30}, {"zorkmi",30}, {"zzmgck",30},
};

static const ZLink ZLINKS[] = {
    {29,295,58}, {36,295,58}, {61,673,58}, {68,295,58}, {87,232,58}, {87,295,58}, {88,673,58}, {98,295,58},
    {112,160,58}, {112,621,66}, {114,342,58}, {116,295,58}, {117,295,58}, {131,467,58}, {148,295,58}, {149,498,82},
    {165,7,58}, {165,63,58}, {165,80,58}, {165,117,58}, {165,240,58}, {165,320,58}, {165,333,58}, {165,456,58},
    {165,509,66}, {165,527,58}, {165,569,58}, {165,586,58}, {165,675,58}, {173,295,58}, {173,607,58}, {183,286,90},
    {232,173,58}, {248,173,58}, {252,160,96}, {252,170,96}, {252,302,58}, {252,393,96}, {252,394,90}, {252,395,82},
    {252,550,96}, {252,551,74}, {252,552,96}, {252,642,96}, {252,662,96}, {253,295,58}, {295,33,82}, {295,93,96},
    {295,358,58}, {298,441,58}, {302,58,58}, {302,93,58}, {317,599,74}, {317,624,90}, {338,58,58}, {344,88,58},
    {344,369,58}, {381,492,58}, {401,327,66}, {405,327,82}, {408,27,58}, {408,80,58}, {408,93,58}, {408,117,58},
    {408,259,58}, {408,342,66}, {408,361,58}, {408,618,74}, {408,670,58}, {419,302,58}, {441,673,58}, {459,681,58},
    {460,29,58}, {460,36,58}, {460,68,58}, {460,87,58}, {460,98,58}, {460,116,58}, {460,117,58}, {460,148,58},
    {460,173,58}, {460,253,58}, {460,419,58}, {460,505,58}, {460,509,58}, {460,537,58}, {460,613,66}, {473,63,58},
    {473,333,58}, {485,41,58}, {490,607,58}, {491,376,58}, {505,295,58}, {509,295,58}, {537,295,58}, {585,673,58},
    {588,7,66}, {588,29,58}, {588,36,58}, {588,41,58}, {588,63,58}, {588,80,58}, {588,87,58}, {588,88,66},
    {588,98,58}, {588,116,66}, {588,117,66}, {588,148,58}, {588,173,58}, {588,177,58}, {588,240,74}, {588,253,58},
    {588,310,58}, {588,320,58}, {588,327,66}, {588,371,58}, {588,419,58}, {588,490,58}, {588,503,58}, {588,505,58},
    {588,509,66}, {588,527,66}, {588,537,66}, {588,586,58}, {588,613,58}, {588,675,58}, {599,673,74}, {605,490,58},
    {607,466,58}, {607,599,58}, {613,295,66}, {618,158,74}, {624,673,90}, {629,61,58}, {629,401,66}, {629,405,82},
    {629,585,58}, {638,259,58}, {642,87,58}, {642,104,58}, {660,505,58}, {668,642,58}, {673,320,74}, {673,369,58},
    {673,456,58}, {673,509,58}, {673,586,90}, {673,675,58}, {681,85,58},
};

void build_zork_typeahead(TrieNode* root) {
    const int nwords = (int)(sizeof(ZWORDS) / sizeof(ZWORDS[0]));
    const int nlinks = (int)(sizeof(ZLINKS) / sizeof(ZLINKS[0]));
    DictionaryWord** w = (DictionaryWord**)TYPEAHEAD_MALLOC(nwords * sizeof(DictionaryWord*));
    for (int i = 0; i < nwords; i++) {
        w[i] = create_word(ZWORDS[i].t, TYPE_UNKNOWN, ZWORDS[i].wt);
        insert_trie(root, w[i]);
    }
    for (int i = 0; i < nlinks; i++) {
        add_next_word(w[ZLINKS[i].a], w[ZLINKS[i].b], ZLINKS[i].wt);
    }
    TYPEAHEAD_FREE(w);
}
