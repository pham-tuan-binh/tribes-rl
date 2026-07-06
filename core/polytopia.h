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
// Unit turn-status FSM (exact port of Unit.canTransitionTo / transitionToStatus,
// Unit.java:96-188). Quirks preserved:
//   - FINISHED is reachable from any non-FINISHED state; FINISHED is absorbing.
//   - SUPERUNIT validates like the Dash group but ANY transition lands FINISHED.
//   - RIDER "Escape": FRESH->ATTACKED->MOVED_AND_ATTACKED->(move)->FINISHED,
//     i.e. attack + up to two moves, or move+attack+move.
//   - KNIGHT "Persist": attacks land FINISHED, but addKill() force-sets ATTACKED
//     (bypassing the FSM) from which another ATTACKED transition is legal.
// ---------------------------------------------------------------------------

static inline bool unit_can_transition(UnitType type, TurnStatus status, TurnStatus to) {
    if (status == STATUS_FINISHED) return false;
    if (to == STATUS_FINISHED) return true;
    switch (type) {
        case UNIT_MIND_BENDER:
        case UNIT_CATAPULT:
        case UNIT_DEFENDER:   // move OR attack, once
            return (to == STATUS_MOVED || to == STATUS_ATTACKED) && status == STATUS_FRESH;
        case UNIT_ARCHER:
        case UNIT_BATTLESHIP:
        case UNIT_BOAT:
        case UNIT_SHIP:
        case UNIT_WARRIOR:
        case UNIT_SWORDMAN:
        case UNIT_SUPERUNIT:  // Dash
            if (to == STATUS_MOVED && status == STATUS_FRESH) return true;
            if (to == STATUS_ATTACKED && (status == STATUS_FRESH || status == STATUS_MOVED)) return true;
            return false;
        case UNIT_RIDER:      // Escape
            if (to == STATUS_MOVED && (status == STATUS_FRESH || status == STATUS_ATTACKED ||
                                       status == STATUS_MOVED_AND_ATTACKED)) return true;
            if (to == STATUS_ATTACKED && (status == STATUS_FRESH || status == STATUS_MOVED)) return true;
            return false;
        case UNIT_KNIGHT:     // Persist
            if (to == STATUS_MOVED && status == STATUS_FRESH) return true;
            if (to == STATUS_ATTACKED && (status == STATUS_FRESH || status == STATUS_MOVED ||
                                          status == STATUS_ATTACKED)) return true;
            return false;
        default:
            return false;
    }
}

// Applies the transition (no-op if illegal, mirroring Java).
static inline void unit_transition(Unit* u, TurnStatus to) {
    if (!unit_can_transition((UnitType)u->type, (TurnStatus)u->status, to)) return;
    if (to == STATUS_FINISHED) { u->status = STATUS_FINISHED; return; }
    TurnStatus s = (TurnStatus)u->status;
    switch ((UnitType)u->type) {
        case UNIT_MIND_BENDER:
        case UNIT_CATAPULT:
        case UNIT_DEFENDER:
        case UNIT_SUPERUNIT:
            u->status = STATUS_FINISHED;
            break;
        case UNIT_ARCHER:
        case UNIT_BATTLESHIP:
        case UNIT_BOAT:
        case UNIT_SHIP:
        case UNIT_WARRIOR:
        case UNIT_SWORDMAN:
            if (to == STATUS_MOVED && s == STATUS_FRESH) u->status = STATUS_MOVED;
            if (to == STATUS_ATTACKED && (s == STATUS_FRESH || s == STATUS_MOVED)) u->status = STATUS_FINISHED;
            break;
        case UNIT_RIDER:
            if (to == STATUS_MOVED && s == STATUS_FRESH) u->status = STATUS_MOVED;
            else if (to == STATUS_MOVED && s == STATUS_ATTACKED) u->status = STATUS_MOVED_AND_ATTACKED;
            else if (to == STATUS_MOVED && s == STATUS_MOVED_AND_ATTACKED) u->status = STATUS_FINISHED;
            else if (to == STATUS_ATTACKED && s == STATUS_FRESH) u->status = STATUS_ATTACKED;
            else if (to == STATUS_ATTACKED && s == STATUS_MOVED) u->status = STATUS_MOVED_AND_ATTACKED;
            break;
        case UNIT_KNIGHT:
            if (to == STATUS_MOVED && s == STATUS_FRESH) u->status = STATUS_MOVED;
            else if (to == STATUS_ATTACKED) u->status = STATUS_FINISHED;  // from FRESH/MOVED/ATTACKED
            break;
        default:
            break;
    }
}

// Unit.addKill — Knight Persist: a kill re-arms the knight's attack.
static inline void unit_add_kill(Unit* u) {
    u->kills++;
    if (u->type == UNIT_KNIGHT) u->status = STATUS_ATTACKED;
}

static inline bool unit_can_attack(const Unit* u) {
    return unit_can_transition((UnitType)u->type, (TurnStatus)u->status, STATUS_ATTACKED);
}
static inline bool unit_can_move(const Unit* u) {
    return unit_can_transition((UnitType)u->type, (TurnStatus)u->status, STATUS_MOVED);
}

// ---------------------------------------------------------------------------
// Fog-of-war bitset helpers
// ---------------------------------------------------------------------------
static inline bool obs_get(const Player* p, int t) { return (p->obs[t >> 3] >> (t & 7)) & 1u; }
static inline void obs_set(Player* p, int t) { p->obs[t >> 3] |= (uint8_t)(1u << (t & 7)); }

// Board.traversable — tech-gated terrain entry.
static inline bool poly_traversable(const PolyState* s, int x, int y, int player) {
    Terrain t = (Terrain)s->terrain[tile_idx(s, x, y)];
    const Player* p = &s->players[player];
    if (t == TERRAIN_MOUNTAIN && !tech_researched(p, TECH_CLIMBING)) return false;
    if (t == TERRAIN_SHALLOW_WATER && !tech_researched(p, TECH_SAILING)) return false;
    if (t == TERRAIN_DEEP_WATER && !tech_researched(p, TECH_NAVIGATION)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Movement (exact port of StepMove.getNeighbours + Pathfinder Dijkstra).
// Costs are doubles, as in Java: road-halving creates fractional costs that do
// not stay on half-steps (e.g. remaining 1.5 halved = 0.75).
// Quirk preserved from StepMove.java:136: the road-discount recheck reads the
// SOURCE tile's city control, not the destination's.
// poly_reachable() fills dest_ok[t]=1 for every legal Move destination
// (reachable, empty, != start) and returns their count.
// ---------------------------------------------------------------------------

static inline bool poly_is_road_like(const PolyState* s, int t) {
    return s->road[t] || s->terrain[t] == TERRAIN_CITY;
}
// "friendly or neutral" road/city tile from the mover's perspective
static inline bool poly_road_usable(const PolyState* s, int t, int player) {
    int8_t c = s->city_at[t];
    return c == -1 || s->cities[c].owner == player;
}

static inline int poly_reachable(const PolyState* s, int unit_idx, uint8_t* dest_ok) {
    const Unit* u = &s->units[unit_idx];
    int n = s->size, ntiles = n * n;
    int player = u->owner;
    double mov = UNIT_STATS[u->type].mov;
    bool is_water = UNIT_STATS[u->type].water;

    double cost[MAX_TILES];
    uint8_t done[MAX_TILES];
    for (int i = 0; i < ntiles; i++) { cost[i] = 1e18; done[i] = 0; dest_ok[i] = 0; }
    int start = tile_idx(s, u->x, u->y);
    cost[start] = 0.0;

    for (;;) {
        // extract-min (boards are <=576 tiles; O(V^2) is fine for v1)
        int cur = -1; double best = 1e18;
        for (int i = 0; i < ntiles; i++)
            if (!done[i] && cost[i] < best) { best = cost[i]; cur = i; }
        if (cur < 0) break;
        done[cur] = 1;

        double cost_from = cost[cur];
        if (cost_from == mov) continue;              // StepMove: movement exhausted
        int cx = cur % n, cy = cur / n;

        // source on friendly/neutral road or city? (road discount precondition)
        bool on_road = poly_is_road_like(s, cur) && poly_road_usable(s, cur, player);

        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int x = cx + dx, y = cy + dy;
            if (!in_bounds(s, x, y)) continue;
            int t = tile_idx(s, x, y);
            Terrain terrain = (Terrain)s->terrain[t];

            // can't path through enemy units
            int16_t other = s->unit_at[t];
            if (other >= 0 && s->units[other].owner != player) continue;
            // can't move into undiscovered tiles
            if (!obs_get(&s->players[player], t)) continue;
            // tech-gated terrain
            if (!poly_traversable(s, x, y, player)) continue;
            // mind benders can't enter enemy city tiles
            if (u->type == UNIT_MIND_BENDER && terrain == TERRAIN_CITY) {
                int8_t c = s->city_at[t];
                if (c >= 0 && s->cities[c].owner != player) continue;
            }

            // zone of control: enemy unit adjacent to destination
            bool zoc = false;
            for (int ay = -1; ay <= 1 && !zoc; ay++) for (int ax = -1; ax <= 1; ax++) {
                if (!ax && !ay) continue;
                int zx = x + ax, zy = y + ay;
                if (!in_bounds(s, zx, zy)) continue;
                int16_t zu = s->unit_at[tile_idx(s, zx, zy)];
                if (zu >= 0 && s->units[zu].owner != player) { zoc = true; break; }
            }

            double remaining = cost_from < mov ? mov - cost_from : mov;
            double step;
            if (is_water) {
                step = terrain_is_water(terrain) || terrain == TERRAIN_FOG ? 1.0
                     : remaining;                                   // disembark
            } else {
                if (terrain_is_water(terrain)) {
                    if (s->building[t] != BUILDING_PORT) continue;  // embark only via port
                    step = remaining;
                } else if (terrain == TERRAIN_FOREST || terrain == TERRAIN_MOUNTAIN) {
                    step = remaining;
                } else {
                    step = 1.0;
                }
                // road discount (ground only) — source-city recheck quirk kept
                if (on_road && poly_is_road_like(s, t) && poly_road_usable(s, cur, player)) {
                    step = step / 2.0 < 0.5 ? 0.5 : step / 2.0;
                }
            }

            bool allowed;
            if (zoc) {                                  // ZoC consumes all remaining movement
                step = remaining;
                allowed = cost_from + step <= mov;
            } else {                                    // Math.floor(costFrom+stepCost) <= MOV
                allowed = (double)(int64_t)(cost_from + step) <= mov;
            }
            if (!allowed) continue;

            double total = cost_from + step;
            if (total < cost[t]) cost[t] = total;
        }
    }

    // legal destinations: reached, not the start tile, empty of any unit
    int count = 0;
    for (int t = 0; t < ntiles; t++) {
        if (t == start || cost[t] >= 1e18) continue;
        if (s->unit_at[t] >= 0) continue;
        dest_ok[t] = 1;
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Push (Board.pushUnit order: S,W,N,E,SW,NW,NE,NE — arrays verbatim
// from Board.java:295-296; coordinates are (x=col, y=row) in our layout)
// ---------------------------------------------------------------------------
static const int8_t PUSH_DX[8] = {0, -1, 0, 1, -1, -1, 1, 1};
static const int8_t PUSH_DY[8] = {1, 0, -1, 0, 1, -1, -1, 1};

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
