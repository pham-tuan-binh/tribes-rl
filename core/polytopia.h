// polytopia-rl — headless game engine (single header, PufferLib Ocean style)
// Rules source: GAIGResearch/Tribes (see docs/spec-01, spec-02).
// Design rules for this file:
//   - POD state, fixed-size arrays, zero heap allocation after init
//   - no I/O, no rendering, no globals; all randomness via env->rng (rand_r-style)
//   - integer game state only; floats appear solely inside pure math helpers
#ifndef POLYTOPIA_H
#define POLYTOPIA_H

#include <stdint.h>
#include <stdbool.h>
#include "constants.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

typedef struct {
    int8_t type;          // UnitType, UNIT_NONE = dead/free slot
    int8_t owner;         // player index
    int8_t carried;       // UnitType carried by naval unit, UNIT_NONE otherwise
    int8_t x, y;
    int16_t hp, max_hp;   // max_hp varies with veteran (+5)
    int8_t kills;
    bool veteran;
    int8_t status;        // TurnStatus
    int8_t city;          // owning city index, -1 = tribe-owned ("extra" unit)
} Unit;

typedef struct {
    int8_t owner;         // player index, -1 = neutral (village not yet a city slot)
    int8_t x, y;
    int8_t level;
    int16_t population;         // may go negative down to -level
    int16_t population_need;    // starts 2; after level-up = level+1
    int16_t production;         // accumulated building production bonuses
    int16_t points_worth;       // score transferred on capture
    bool is_capital;
    bool has_walls;
    int8_t bound;               // border radius, starts 1
    int8_t num_units;           // units housed (cap = level+1)
    // temple state is tracked per building via the building arrays below
} City;

typedef struct {
    int8_t tribe;               // TribeType
    int32_t stars;
    int32_t score;
    int8_t capital;             // city index of original capital
    int8_t num_kills;
    int8_t pacifist_count;
    uint32_t techs;             // bitset over Tech (NUM_TECH = 24 fits in u32)
    uint8_t monuments[NUM_MONUMENTS];  // MonumentStatus, indexed building-12
    GameResult result;
    // fog of war: bitset over tiles, kept even in full-obs mode
    uint8_t obs[(MAX_TILES + 7) / 8];
} Player;

typedef struct {
    // --- per-tile board planes (flat [y*size+x], fixed max footprint) ---
    int8_t terrain[MAX_TILES];      // Terrain
    int8_t resource[MAX_TILES];     // Resource (RESOURCE_NONE = empty)
    int8_t building[MAX_TILES];     // BuildingType (BUILDING_NONE = empty)
    int16_t unit_at[MAX_TILES];     // unit index, -1 = empty
    int8_t city_at[MAX_TILES];      // owning city index (border), -1 = unowned
    uint8_t road[MAX_TILES];        // road/trade-network membership
    // temple levels/counters, parallel to building plane (0 unless temple there)
    int8_t temple_level[MAX_TILES];
    int8_t temple_turns[MAX_TILES];

    // --- actors ---
    Unit units[MAX_UNITS];
    City cities[MAX_CITIES];
    Player players[MAX_PLAYERS];
    int16_t num_units;              // high-water mark in units[]
    int8_t num_cities;              // cities + not-yet-captured villages get slots on capture
    int8_t num_players;

    // --- game flow ---
    int8_t size;                    // board side (11..24)
    GameMode mode;
    int16_t tick;                   // full rounds elapsed
    int16_t max_turns;
    int8_t active_player;
    bool leveling_up;               // a city must pick a level-up bonus now
    int8_t leveling_city;
    bool game_over;

    uint32_t rng;                   // xorshift/rand_r state; seeded at reset
} PolyState;

// ---------------------------------------------------------------------------
// Tile helpers
// ---------------------------------------------------------------------------
static inline int tile_idx(const PolyState* s, int x, int y) { return y * s->size + x; }
static inline bool in_bounds(const PolyState* s, int x, int y) {
    return x >= 0 && y >= 0 && x < s->size && y < s->size;
}
static inline int chebyshev(int x0, int y0, int x1, int y1) {
    int dx = x0 > x1 ? x0 - x1 : x1 - x0;
    int dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return dx > dy ? dx : dy;
}

// rand_r-compatible LCG (POSIX rand_r semantics not required — just fast,
// seedable, per-env). Returns [0, 2^31).
static inline uint32_t poly_rand(uint32_t* rng) {
    *rng = *rng * 1103515245u + 12345u;
    return (*rng >> 1) & 0x7fffffffu;
}
static inline int poly_rand_int(uint32_t* rng, int n) { return (int)(poly_rand(rng) % (uint32_t)n); }

// ---------------------------------------------------------------------------
// Pure rules math (mirrors Java exactly; unit-tested)
// ---------------------------------------------------------------------------

// Java Math.round(double) == floor(x + 0.5); for our positive operands this is it.
static inline int java_round(double x) { return (int)(x + 0.5); }

// Tech: TechnologyTree.getCost — base + tier*numCities, ×0.2 int-truncated with PHILOSOPHY.
static inline int tech_cost(Tech t, int num_cities, bool has_philosophy) {
    int cost = TECH_BASE_COST + TECH_INFO[t].tier * num_cities;
    if (has_philosophy) cost = (int)(cost * TECH_DISCOUNT_VALUE);
    return cost;
}

static inline bool tech_researched(const Player* p, Tech t) { return (p->techs >> t) & 1u; }
static inline bool tech_researchable(const Player* p, Tech t) {
    if (tech_researched(p, t)) return false;
    int8_t parent = TECH_INFO[t].parent;
    return parent == TECH_NONE || tech_researched(p, (Tech)parent);
}

// Combat: AttackCommand.getAttackResults. def_bonus is 1.0 / 1.5 / 4.0 (see
// combat_def_bonus below). Outputs: damage to target, retaliation to attacker.
typedef struct { int to_target; int to_attacker; } CombatResult;

static inline CombatResult combat_results(
    int atk, int atk_hp, int atk_max_hp,
    int def, int def_hp, int def_max_hp,
    double def_bonus)
{
    double attack_force = atk * ((double)atk_hp / atk_max_hp);
    double defence_force = def * ((double)def_hp / def_max_hp) * def_bonus;
    double total = attack_force + defence_force;
    CombatResult r;
    if (total <= 0.0) { r.to_target = 0; r.to_attacker = 0; return r; }  // 0-atk vs 0-def guard
    r.to_target = java_round(attack_force / total * atk * ATTACK_MODIFIER);
    r.to_attacker = java_round(defence_force / total * def * ATTACK_MODIFIER);
    return r;
}

// Defence bonus for a target standing on `terrain` (see AttackCommand):
//   own city + walls -> 4.0; own city + fortify-capable -> 1.5;
//   mountain+MEDITATION / water+AQUATISM / forest+ARCHERY -> 1.5; else 1.0.
static inline double combat_def_bonus(
    Terrain terrain, bool in_own_city, bool city_has_walls,
    bool target_can_fortify, const Player* target_owner)
{
    if (terrain == TERRAIN_CITY && in_own_city) {
        if (city_has_walls) return DEFENCE_IN_WALLS;
        if (target_can_fortify) return DEFENCE_BONUS;
        return 1.0;
    }
    if ((terrain == TERRAIN_MOUNTAIN && tech_researched(target_owner, TECH_MEDITATION)) ||
        (terrain_is_water((Terrain)terrain) && tech_researched(target_owner, TECH_AQUATISM)) ||
        (terrain == TERRAIN_FOREST && tech_researched(target_owner, TECH_ARCHERY))) {
        return DEFENCE_BONUS;
    }
    return 1.0;
}

// City production: City.getProduction — pop >= 0 ? level + production (+1 capital) : pop.
static inline int city_production(const City* c) {
    if (c->population < 0) return c->population;
    return c->level + c->production + (c->is_capital ? PROD_CAPITAL_BONUS : 0);
}

// ---------------------------------------------------------------------------
// Engine API (implemented across M1; declarations fixed now so binding.c,
// tests and the WASM bridge can build against them)
// ---------------------------------------------------------------------------

// Initialize a fresh game: procgen map from seed, place capitals & starting units.
void poly_reset(PolyState* s, int num_players, const int8_t* tribes, int size,
                GameMode mode, uint32_t seed);

// Legal-action mask for the active player at the current phase.
// `mask` has POLY_ACTION_SPACE bits; sized by the acting layer (puffer/web).
// Defined in actions.h (M1).

// Apply one atomic action for the active player. Returns false if illegal.
bool poly_apply(PolyState* s, int32_t action);

// True when the game has ended; results are in players[i].result.
static inline bool poly_done(const PolyState* s) { return s->game_over; }

#endif // POLYTOPIA_H
