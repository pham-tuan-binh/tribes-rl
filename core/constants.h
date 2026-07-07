// polytopia-rl — game constants transcribed from GAIGResearch/Tribes
// Sources: reference/Tribes/src/core/TribesConfig.java, Types.java
// Every value here is verified against the Java source; enum integer values
// match the Java keys / ordinals exactly (they index arrays in both engines).
#ifndef POLY_CONSTANTS_H
#define POLY_CONSTANTS_H

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Compile-time capacities
// ---------------------------------------------------------------------------
#define MAX_MAP_SIZE 24            // DEFAULT_MAP_SIZE for 8 tribes
#define MAX_TILES (MAX_MAP_SIZE * MAX_MAP_SIZE)
#define MAX_PLAYERS 4              // v1 trains 2; engine supports up to 4
#define MAX_UNITS 256              // hard pool cap (board tiles bound live units)
#define MAX_CITIES 64              // villages + capitals on the largest maps
#define MAX_BUILDINGS_PER_CITY 32

// Tribes' map size per player count: DEFAULT_MAP_SIZE[nTribes-1], -1 invalid.
static const int8_t DEFAULT_MAP_SIZE[8] = {-1, 11, 14, 16, 18, 20, 22, 24};

// ---------------------------------------------------------------------------
// Terrain (Types.TERRAIN — keys 0..7)
// ---------------------------------------------------------------------------
typedef enum {
    TERRAIN_PLAIN = 0,
    TERRAIN_SHALLOW_WATER = 1,
    TERRAIN_DEEP_WATER = 2,
    TERRAIN_MOUNTAIN = 3,
    TERRAIN_VILLAGE = 4,
    TERRAIN_CITY = 5,
    TERRAIN_FOREST = 6,
    TERRAIN_FOG = 7,
    NUM_TERRAIN = 8
} Terrain;

static inline bool terrain_is_water(Terrain t) {
    return t == TERRAIN_SHALLOW_WATER || t == TERRAIN_DEEP_WATER;
}

// ---------------------------------------------------------------------------
// Resources (Types.RESOURCE — Java keys {0,1,2,3,5,6,7}; key 4 unused there.
// We keep the Java keys for trace compatibility; RESOURCE_NONE fills the gap.)
// ---------------------------------------------------------------------------
typedef enum {
    RESOURCE_FISH = 0,
    RESOURCE_FRUIT = 1,
    RESOURCE_ANIMAL = 2,
    RESOURCE_WHALES = 3,
    RESOURCE_NONE = 4,      // unused key in Java; our "no resource" sentinel
    RESOURCE_ORE = 5,
    RESOURCE_CROPS = 6,
    RESOURCE_RUINS = 7,
    NUM_RESOURCE = 8
} Resource;

// ---------------------------------------------------------------------------
// Technologies (Types.TECHNOLOGY — ordinal order = declaration order,
// verified at Types.java:25-48; this order indexes researched[] bitsets)
// ---------------------------------------------------------------------------
typedef enum {
    TECH_CLIMBING = 0,
    TECH_FISHING = 1,
    TECH_HUNTING = 2,
    TECH_ORGANIZATION = 3,
    TECH_RIDING = 4,
    TECH_ARCHERY = 5,
    TECH_FARMING = 6,
    TECH_FORESTRY = 7,
    TECH_FREE_SPIRIT = 8,
    TECH_MEDITATION = 9,
    TECH_MINING = 10,
    TECH_ROADS = 11,
    TECH_SAILING = 12,
    TECH_SHIELDS = 13,
    TECH_WHALING = 14,
    TECH_AQUATISM = 15,
    TECH_CHIVALRY = 16,
    TECH_CONSTRUCTION = 17,
    TECH_MATHEMATICS = 18,
    TECH_NAVIGATION = 19,
    TECH_SMITHERY = 20,
    TECH_SPIRITUALISM = 21,
    TECH_TRADE = 22,
    TECH_PHILOSOPHY = 23,
    NUM_TECH = 24,
    TECH_NONE = -1
} Tech;

typedef struct {
    int8_t tier;    // 1..3
    int8_t parent;  // Tech or TECH_NONE
} TechInfo;

static const TechInfo TECH_INFO[NUM_TECH] = {
    [TECH_CLIMBING]     = {1, TECH_NONE},
    [TECH_FISHING]      = {1, TECH_NONE},
    [TECH_HUNTING]      = {1, TECH_NONE},
    [TECH_ORGANIZATION] = {1, TECH_NONE},
    [TECH_RIDING]       = {1, TECH_NONE},
    [TECH_ARCHERY]      = {2, TECH_HUNTING},
    [TECH_FARMING]      = {2, TECH_ORGANIZATION},
    [TECH_FORESTRY]     = {2, TECH_HUNTING},
    [TECH_FREE_SPIRIT]  = {2, TECH_RIDING},
    [TECH_MEDITATION]   = {2, TECH_CLIMBING},
    [TECH_MINING]       = {2, TECH_CLIMBING},
    [TECH_ROADS]        = {2, TECH_RIDING},
    [TECH_SAILING]      = {2, TECH_FISHING},
    [TECH_SHIELDS]      = {2, TECH_ORGANIZATION},
    [TECH_WHALING]      = {2, TECH_FISHING},
    [TECH_AQUATISM]     = {3, TECH_WHALING},
    [TECH_CHIVALRY]     = {3, TECH_FREE_SPIRIT},
    [TECH_CONSTRUCTION] = {3, TECH_FARMING},
    [TECH_MATHEMATICS]  = {3, TECH_FORESTRY},
    [TECH_NAVIGATION]   = {3, TECH_SAILING},
    [TECH_SMITHERY]     = {3, TECH_MINING},
    [TECH_SPIRITUALISM] = {3, TECH_ARCHERY},
    [TECH_TRADE]        = {3, TECH_ROADS},
    [TECH_PHILOSOPHY]   = {3, TECH_MEDITATION},
};

// ResearchTech cost: TECH_BASE_COST + tier * numCities; ×0.2 (int-trunc) with PHILOSOPHY.
#define TECH_BASE_COST 4
#define TECH_DISCOUNT_TECH TECH_PHILOSOPHY
#define TECH_DISCOUNT_VALUE 0.2
#define TECH_TIER_POINTS 100

// ---------------------------------------------------------------------------
// Units (Types.UNIT — keys 0..11; stats TribesConfig.java:5-111)
// ---------------------------------------------------------------------------
typedef enum {
    UNIT_WARRIOR = 0,
    UNIT_RIDER = 1,
    UNIT_DEFENDER = 2,
    UNIT_SWORDMAN = 3,
    UNIT_ARCHER = 4,
    UNIT_CATAPULT = 5,
    UNIT_KNIGHT = 6,
    UNIT_MIND_BENDER = 7,
    UNIT_BOAT = 8,
    UNIT_SHIP = 9,
    UNIT_BATTLESHIP = 10,
    UNIT_SUPERUNIT = 11,
    NUM_UNIT = 12,
    UNIT_NONE = -1
} UnitType;

typedef struct {
    int8_t atk, def, mov;
    int8_t max_hp;      // 0 for water units (inherited from carried land unit)
    int8_t range;
    int8_t cost;
    int8_t points;      // score awarded on spawn (POINTS in TribesConfig)
    int8_t tech_req;    // Tech or TECH_NONE
    bool spawnable;     // Types.UNIT.spawnable()
    bool water;         // isWaterUnit()
    bool ranged;        // isRanged()
    bool fortify;       // canFortify()
    bool melee_push;    // advances onto tile on melee kill (AttackCommand list)
} UnitStats;

static const UnitStats UNIT_STATS[NUM_UNIT] = {
    //                     atk def mov hp range cost pts  tech               spawn  water  ranged fortify push
    [UNIT_WARRIOR]     = {  2,  2,  1, 10, 1,    2, 10, TECH_NONE,           true,  false, false, true,  true  },
    [UNIT_RIDER]       = {  2,  1,  2, 10, 1,    3, 15, TECH_RIDING,         true,  false, false, true,  true  },
    [UNIT_DEFENDER]    = {  1,  3,  1, 15, 1,    3, 15, TECH_SHIELDS,        true,  false, false, true,  true  },
    [UNIT_SWORDMAN]    = {  3,  3,  1, 15, 1,    5, 25, TECH_SMITHERY,       true,  false, false, true,  true  },
    [UNIT_ARCHER]      = {  2,  1,  1, 10, 2,    3, 15, TECH_ARCHERY,        true,  false, true,  true,  false },
    [UNIT_CATAPULT]    = {  4,  0,  1, 10, 3,    8, 40, TECH_MATHEMATICS,    true,  false, true,  false, false },
    [UNIT_KNIGHT]      = {  4,  1,  3, 15, 1,    8, 40, TECH_CHIVALRY,       true,  false, false, true,  true  },
    [UNIT_MIND_BENDER] = {  0,  1,  1, 10, 1,    5, 25, TECH_PHILOSOPHY,     true,  false, false, false, false },
    [UNIT_BOAT]        = {  1,  1,  2,  0, 2,    0,  0, TECH_SAILING,        false, true,  true,  false, false },
    [UNIT_SHIP]        = {  2,  2,  3,  0, 2,    5,  0, TECH_SAILING,        false, true,  true,  false, false },
    [UNIT_BATTLESHIP]  = {  4,  3,  3,  0, 2,   15,  0, TECH_NAVIGATION,     false, true,  true,  false, false },
    [UNIT_SUPERUNIT]   = {  5,  4,  1, 40, 1,   10, 50, TECH_NONE,           false, false, false, false, true  },
};

// General unit constants (TribesConfig.java:113-123)
#define EXPLORER_NUM_STEPS 15
#define ATTACK_MODIFIER 4.5
#define DEFENCE_BONUS 1.5
#define DEFENCE_IN_WALLS 4.0
#define VETERAN_KILLS 3
#define VETERAN_PLUS_HP 5
#define RECOVER_PLUS_HP 2
#define RECOVER_IN_BORDERS_PLUS_HP 2
#define MINDBENDER_HEAL 4

// Unit turn status (Types.TURN_STATUS ordinals)
typedef enum {
    STATUS_FRESH = 0,
    STATUS_MOVED = 1,
    STATUS_ATTACKED = 2,
    STATUS_MOVED_AND_ATTACKED = 3,
    STATUS_PUSHED = 4,
    STATUS_FINISHED = 5
} TurnStatus;

// ---------------------------------------------------------------------------
// Buildings (Types.BUILDING — keys 0..18; costs TribesConfig.java:125-177)
// ---------------------------------------------------------------------------
typedef enum {
    BUILDING_PORT = 0,
    BUILDING_MINE = 1,
    BUILDING_FORGE = 2,
    BUILDING_FARM = 3,
    BUILDING_WINDMILL = 4,
    BUILDING_CUSTOMS_HOUSE = 5,
    BUILDING_LUMBER_HUT = 6,
    BUILDING_SAWMILL = 7,
    BUILDING_TEMPLE = 8,
    BUILDING_WATER_TEMPLE = 9,
    BUILDING_FOREST_TEMPLE = 10,
    BUILDING_MOUNTAIN_TEMPLE = 11,
    BUILDING_ALTAR_OF_PEACE = 12,
    BUILDING_EMPERORS_TOMB = 13,
    BUILDING_EYE_OF_GOD = 14,
    BUILDING_GATE_OF_POWER = 15,
    BUILDING_GRAND_BAZAR = 16,
    BUILDING_PARK_OF_FORTUNE = 17,
    BUILDING_TOWER_OF_WISDOM = 18,
    NUM_BUILDING = 19,
    BUILDING_NONE = -1
} BuildingType;

// terrain_mask bit i = allowed on Terrain i
#define TMASK(t) (1u << (t))

typedef struct {
    int8_t cost;
    int8_t bonus;             // production (base/pair) or population (monument/temple context)
    int8_t tech_req;          // Tech or TECH_NONE
    uint8_t terrain_mask;
    int8_t resource_req;      // Resource required on tile, or RESOURCE_NONE
    int8_t adj_req;           // must be adjacent to this BuildingType, or BUILDING_NONE
    bool is_base;             // FARM | MINE | LUMBER_HUT
    bool is_temple;
    bool is_monument;
} BuildingInfo;

static const BuildingInfo BUILDING_INFO[NUM_BUILDING] = {
    //                                cost bonus tech                 terrain_mask                                        res_req         adj_req               base   temple monument
    [BUILDING_PORT]            = { 10, 2, TECH_SAILING,      TMASK(TERRAIN_SHALLOW_WATER),                        RESOURCE_NONE,  BUILDING_NONE,        false, false, false },
    [BUILDING_MINE]            = {  5, 2, TECH_MINING,       TMASK(TERRAIN_MOUNTAIN),                             RESOURCE_ORE,   BUILDING_NONE,        true,  false, false },
    [BUILDING_FORGE]           = {  5, 2, TECH_SMITHERY,     TMASK(TERRAIN_PLAIN),                                RESOURCE_NONE,  BUILDING_MINE,        false, false, false },
    [BUILDING_FARM]            = {  5, 2, TECH_FARMING,      TMASK(TERRAIN_PLAIN),                                RESOURCE_CROPS, BUILDING_NONE,        true,  false, false },
    [BUILDING_WINDMILL]        = {  5, 1, TECH_CONSTRUCTION, TMASK(TERRAIN_PLAIN),                                RESOURCE_NONE,  BUILDING_FARM,        false, false, false },
    [BUILDING_CUSTOMS_HOUSE]   = {  5, 2, TECH_TRADE,        TMASK(TERRAIN_PLAIN),                                RESOURCE_NONE,  BUILDING_PORT,        false, false, false },
    [BUILDING_LUMBER_HUT]      = {  2, 1, TECH_FORESTRY,     TMASK(TERRAIN_FOREST),                               RESOURCE_NONE,  BUILDING_NONE,        true,  false, false },
    [BUILDING_SAWMILL]         = {  5, 1, TECH_MATHEMATICS,  TMASK(TERRAIN_PLAIN),                                RESOURCE_NONE,  BUILDING_LUMBER_HUT,  false, false, false },
    [BUILDING_TEMPLE]          = { 20, 1, TECH_FREE_SPIRIT,  TMASK(TERRAIN_PLAIN),                                RESOURCE_NONE,  BUILDING_NONE,        false, true,  false },
    [BUILDING_WATER_TEMPLE]    = { 20, 1, TECH_AQUATISM,     TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_DEEP_WATER), RESOURCE_NONE, BUILDING_NONE,      false, true,  false },
    [BUILDING_FOREST_TEMPLE]   = { 15, 1, TECH_SPIRITUALISM, TMASK(TERRAIN_FOREST),                               RESOURCE_NONE,  BUILDING_NONE,        false, true,  false },
    [BUILDING_MOUNTAIN_TEMPLE] = { 20, 1, TECH_MEDITATION,   TMASK(TERRAIN_MOUNTAIN),                             RESOURCE_NONE,  BUILDING_NONE,        false, true,  false },
    [BUILDING_ALTAR_OF_PEACE]  = {  0, 3, TECH_MEDITATION,   TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_PLAIN),   RESOURCE_NONE,  BUILDING_NONE,        false, false, true  },
    [BUILDING_EMPERORS_TOMB]   = {  0, 3, TECH_TRADE,        TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_PLAIN),   RESOURCE_NONE,  BUILDING_NONE,        false, false, true  },
    [BUILDING_EYE_OF_GOD]      = {  0, 3, TECH_NAVIGATION,   TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_PLAIN),   RESOURCE_NONE,  BUILDING_NONE,        false, false, true  },
    [BUILDING_GATE_OF_POWER]   = {  0, 3, TECH_NONE,         TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_PLAIN),   RESOURCE_NONE,  BUILDING_NONE,        false, false, true  },
    [BUILDING_GRAND_BAZAR]     = {  0, 3, TECH_ROADS,        TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_PLAIN),   RESOURCE_NONE,  BUILDING_NONE,        false, false, true  },
    [BUILDING_PARK_OF_FORTUNE] = {  0, 3, TECH_NONE,         TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_PLAIN),   RESOURCE_NONE,  BUILDING_NONE,        false, false, true  },
    [BUILDING_TOWER_OF_WISDOM] = {  0, 3, TECH_PHILOSOPHY,   TMASK(TERRAIN_SHALLOW_WATER)|TMASK(TERRAIN_PLAIN),   RESOURCE_NONE,  BUILDING_NONE,        false, false, true  },
};

// Production pairs (City.applyBonus): base building <-> booster
static inline BuildingType building_pair(BuildingType b) {
    switch (b) {
        case BUILDING_PORT: return BUILDING_CUSTOMS_HOUSE;
        case BUILDING_CUSTOMS_HOUSE: return BUILDING_PORT;
        case BUILDING_FARM: return BUILDING_WINDMILL;
        case BUILDING_WINDMILL: return BUILDING_FARM;
        case BUILDING_MINE: return BUILDING_FORGE;
        case BUILDING_FORGE: return BUILDING_MINE;
        case BUILDING_LUMBER_HUT: return BUILDING_SAWMILL;
        case BUILDING_SAWMILL: return BUILDING_LUMBER_HUT;
        default: return BUILDING_NONE;
    }
}

#define PORT_BONUS_POP 2
#define PORT_TRADE_DISTANCE 4      // includes destination port

// Monuments (TribesConfig.java:162-169)
#define MONUMENT_BONUS 3
#define MONUMENT_POINTS 400
#define EMPERORS_TOMB_STARS 100
#define GATE_OF_POWER_KILLS 10
#define GRAND_BAZAR_CITIES 5
#define ALTAR_OF_PEACE_TURNS 5
#define PARK_OF_FORTUNE_LEVEL 5

typedef enum { MONUMENT_UNAVAILABLE = 0, MONUMENT_AVAILABLE = 1, MONUMENT_BUILT = 2 } MonumentStatus;
#define NUM_MONUMENTS 7   // buildings 12..18

// Temples (TribesConfig.java:172-177)
#define TEMPLE_TURNS_TO_SCORE 3
#define TEMPLE_MAX_LEVEL 5
static const int16_t TEMPLE_POINTS[5] = {100, 50, 50, 50, 150};

// ---------------------------------------------------------------------------
// Resources: costs & effects (TribesConfig.java:180-187)
// ---------------------------------------------------------------------------
typedef struct {
    int8_t cost;
    int8_t pop_bonus;    // population added on gather
    int8_t star_bonus;   // stars added on gather (whales)
    int8_t tech_req;     // gather tech, TECH_NONE for ruins
    int8_t mask_tech;    // tech required to SEE it in partial obs, TECH_NONE if always visible
} ResourceInfo;

static const ResourceInfo RESOURCE_INFO[NUM_RESOURCE] = {
    [RESOURCE_FISH]   = {2, 1,  0, TECH_FISHING,      TECH_FISHING},
    [RESOURCE_FRUIT]  = {2, 1,  0, TECH_ORGANIZATION, TECH_NONE},
    [RESOURCE_ANIMAL] = {2, 1,  0, TECH_HUNTING,      TECH_NONE},
    [RESOURCE_WHALES] = {0, 0, 10, TECH_WHALING,      TECH_FISHING},
    [RESOURCE_NONE]   = {0, 0,  0, TECH_NONE,         TECH_NONE},
    [RESOURCE_ORE]    = {0, 0,  0, TECH_MINING,       TECH_CLIMBING},
    [RESOURCE_CROPS]  = {0, 0,  0, TECH_FARMING,      TECH_ORGANIZATION},
    [RESOURCE_RUINS]  = {0, 0,  0, TECH_NONE,         TECH_NONE},
};

// ---------------------------------------------------------------------------
// Tribes (Types.TRIBE — keys 0..11)
// ---------------------------------------------------------------------------
typedef enum {
    TRIBE_XIN_XI = 0, TRIBE_IMPERIUS = 1, TRIBE_BARDUR = 2, TRIBE_OUMAJI = 3,
    TRIBE_KICKOO = 4, TRIBE_HOODRICK = 5, TRIBE_LUXIDOOR = 6, TRIBE_VENGIR = 7,
    TRIBE_ZEBASI = 8, TRIBE_AI_MO = 9, TRIBE_QUETZALI = 10, TRIBE_YADAKK = 11,
    NUM_TRIBE = 12
} TribeType;

typedef struct {
    int8_t initial_tech;   // Tech or TECH_NONE (Luxidoor)
    int8_t starting_unit;  // UnitType
} TribeInfo;

static const TribeInfo TRIBE_INFO[NUM_TRIBE] = {
    [TRIBE_XIN_XI]   = {TECH_CLIMBING,     UNIT_WARRIOR},
    [TRIBE_IMPERIUS] = {TECH_ORGANIZATION, UNIT_WARRIOR},
    [TRIBE_BARDUR]   = {TECH_HUNTING,      UNIT_WARRIOR},
    [TRIBE_OUMAJI]   = {TECH_RIDING,       UNIT_RIDER},
    [TRIBE_KICKOO]   = {TECH_FISHING,      UNIT_WARRIOR},
    [TRIBE_HOODRICK] = {TECH_ARCHERY,      UNIT_ARCHER},
    [TRIBE_LUXIDOOR] = {TECH_NONE,         UNIT_WARRIOR},  // L3 walled capital instead
    [TRIBE_VENGIR]   = {TECH_SMITHERY,     UNIT_SWORDMAN},
    [TRIBE_ZEBASI]   = {TECH_FARMING,      UNIT_WARRIOR},
    [TRIBE_AI_MO]    = {TECH_MEDITATION,   UNIT_WARRIOR},
    [TRIBE_QUETZALI] = {TECH_SHIELDS,      UNIT_DEFENDER},
    [TRIBE_YADAKK]   = {TECH_ROADS,        UNIT_WARRIOR},
};

#define INITIAL_STARS 5

// ---------------------------------------------------------------------------
// Cities & economy (TribesConfig.java:192-204)
// ---------------------------------------------------------------------------
#define CITY_LEVEL_UP_WORKSHOP_PROD 1
#define CITY_LEVEL_UP_RESOURCES 5
#define CITY_LEVEL_UP_POP_GROWTH 3
#define CITY_LEVEL_UP_PARK 250
#define CITY_BORDER_POINTS 20
#define CITY_CENTRE_POINTS 100
#define PROD_CAPITAL_BONUS 1
#define EXPLORER_CLEAR_RANGE 1
#define FIRST_CITY_CLEAR_RANGE 2
#define NEW_CITY_CLEAR_RANGE 1
#define CITY_EXPANSION_TILES 1
#define POINTS_PER_POPULATION 5

// City level-up choices (Types.CITY_LEVEL_UP): two options per level tier.
typedef enum {
    LEVELUP_WORKSHOP = 0,      // level 1 (i.e. leveling 1->2)
    LEVELUP_EXPLORER = 1,
    LEVELUP_CITY_WALL = 2,     // level 2
    LEVELUP_RESOURCES = 3,
    LEVELUP_POP_GROWTH = 4,    // level 3
    LEVELUP_BORDER_GROWTH = 5,
    LEVELUP_PARK = 6,          // level >= 4
    LEVELUP_SUPERUNIT = 7,
    NUM_LEVELUP = 8
} LevelUpBonus;

// Points on level-up: CITY_LEVEL_UP.getLevelUpPoints() = 50 - reachedLevel*5,
// where reachedLevel is the option's enum field (WORKSHOP/EXPLORER=2 ...
// PARK/SUPERUNIT=5). The Java `level==1 ? 100` branch is dead code.
// Indexed by LevelUpBonus: {40,40,35,35,30,30,25,25}.
static const int8_t LEVELUP_POINTS[NUM_LEVELUP] = {40, 40, 35, 35, 30, 30, 25, 25};

// ---------------------------------------------------------------------------
// Actions & misc (TribesConfig.java:189-227)
// ---------------------------------------------------------------------------
#define ROAD_COST 2
#define CLEAR_FOREST_STAR 2
#define GROW_FOREST_COST 5
#define BURN_FOREST_COST 5
#define CLEAR_VIEW_POINTS 5

// Examine (ruins) bonuses — Types.EXAMINE_BONUS, uniform random
typedef enum {
    EXAMINE_SUPERUNIT = 0,
    EXAMINE_RESEARCH = 1,
    EXAMINE_POP_GROWTH = 2,   // +3 population
    EXAMINE_EXPLORER = 3,
    EXAMINE_RESOURCES = 4,    // +10 stars
    NUM_EXAMINE = 5
} ExamineBonus;
#define EXAMINE_POP_GROWTH_AMOUNT 3
#define EXAMINE_RESOURCES_STARS 10

// Diplomacy (TribesConfig.java:206-211) — NOT USED in v1 (deliberately cut),
// kept for spec completeness / future flags.
#define ALLEGIANCE_MAX 60
#define ATTACK_REPERCUSSION (-5)
#define CAPTURE_REPERCUSSION (-30)
#define CONVERT_REPERCUSSION (-5)
#define MIN_STARS_SEND 15

// ---------------------------------------------------------------------------
// Game modes & turn limits (Types.GAME_MODE, Constants.java)
// ---------------------------------------------------------------------------
typedef enum { MODE_CAPITALS = 0, MODE_SCORE = 1 } GameMode;
#define MAX_TURNS_SCORE 30
#define MAX_TURNS_CAPITALS 50

typedef enum { RESULT_WIN = 0, RESULT_LOSS = 1, RESULT_INCOMPLETE = 2 } GameResult;

#endif // POLY_CONSTANTS_H
