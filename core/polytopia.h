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
#include <string.h>
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

#define CITY_MAX_UNITS 16   // Java list is unbounded; cap = level+1 makes >15 unreachable in practice

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
    int16_t unit_ids[CITY_MAX_UNITS];  // insertion-ordered (Java unitsID list)
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
    // owned cities, insertion-ordered (Java Tribe.citiesID; order feeds
    // the seeded shuffles in capture unit-redistribution)
    int8_t city_list[MAX_CITIES];
    int8_t num_cities;
    uint64_t connected_cities;  // bitset over city index (trade-connected to capital)
    int32_t tiles_seen;         // fog tiles revealed (reward shaping)
    int32_t kill_value;         // summed star-cost of units this player killed
    int32_t lost_value;         // summed star-cost of own units lost (any cause,
                                // incl. disband — prevents the disband-to-deny-
                                // kill-reward exploit)
    int32_t stars_wasted;       // stars burned in do-undo cycles (e.g. grow->clear
                                // forest) — reward shaping charges these directly
    int32_t capitals_taken;     // enemy original capitals captured (multiplayer's
                                // key intermediate objective)
    int32_t capitals_lost;
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
    uint8_t net_tile[MAX_TILES];    // Java TradeNetwork.networkTiles: roads, ports, city centers
    uint8_t grown[MAX_TILES];       // forest was GROWN here (reward accounting: clearing or
                                    // burning a grown forest is star waste; a lumber hut redeems it)
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
    int32_t tick;                   // full rounds elapsed
    int32_t max_turns;
    int8_t active_player;
    bool leveling_up;               // a city must pick a level-up bonus now
    int8_t leveling_city;
    bool game_over;
    bool won_by_domination;         // game ended by all-capitals capture (not turn-cap ranking)
    bool tribes_compat;             // exact GAIGResearch/Tribes rules (golden-trace replay);
                                    // off = product rules (roads on PLAIN only)
    bool fog;                       // partial observability (fog of war)

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
            // Java quirk (Unit.java:172-177): the rider block is sequential
            // NON-else ifs re-reading the mutated status, so a MOVED
            // transition from ATTACKED cascades ATTACKED -> MOVED_AND_ATTACKED
            // -> FINISHED in one call. Attack-then-move ends the rider's turn;
            // only move -> attack -> move gets the full Escape.
            if (to == STATUS_MOVED && s == STATUS_FRESH) u->status = STATUS_MOVED;
            else if (to == STATUS_MOVED && (s == STATUS_ATTACKED || s == STATUS_MOVED_AND_ATTACKED))
                u->status = STATUS_FINISHED;
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

// Board.isRoad: network tile that is not water and not a city center.
static inline bool poly_is_road(const PolyState* s, int t) {
    return s->net_tile[t] && !terrain_is_water((Terrain)s->terrain[t]) &&
           s->terrain[t] != TERRAIN_CITY;
}
// StepMove treats city centers as roads for the discount.
static inline bool poly_is_road_like(const PolyState* s, int t) {
    return poly_is_road(s, t) || s->terrain[t] == TERRAIN_CITY;
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

    // binary min-heap of (cost, tile) with lazy deletion
    struct { double c; int16_t t; } heap[MAX_TILES * 4];
    int hn = 0;
    heap[hn].c = 0.0; heap[hn].t = (int16_t)start; hn++;

    while (hn > 0) {
        // pop min
        double cur_c = heap[0].c;
        int cur = heap[0].t;
        hn--;
        heap[0] = heap[hn];
        for (int i = 0;;) {
            int l = 2 * i + 1, r = l + 1, m = i;
            if (l < hn && heap[l].c < heap[m].c) m = l;
            if (r < hn && heap[r].c < heap[m].c) m = r;
            if (m == i) break;
            { __typeof__(heap[0]) tmp = heap[i]; heap[i] = heap[m]; heap[m] = tmp; }
            i = m;
        }
        if (done[cur] || cur_c > cost[cur]) continue;   // stale entry
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
            if (total < cost[t]) {
                cost[t] = total;
                if (hn < (int)(sizeof(heap) / sizeof(heap[0]))) {   // push
                    int i = hn++;
                    heap[i].c = total; heap[i].t = (int16_t)t;
                    while (i > 0) {
                        int par = (i - 1) / 2;
                        if (heap[par].c <= heap[i].c) break;
                        { __typeof__(heap[0]) tmp = heap[i]; heap[i] = heap[par]; heap[par] = tmp; }
                        i = par;
                    }
                }
            }
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
// City economy & building effects (City.java, Temple state in board planes)
// ---------------------------------------------------------------------------

// City.addPopulation(tribe, value): clamp at -level; score goes to the PASSED
// player (not necessarily the city's owner — e.g. network drops on cities the
// player just lost), pointsWorth stays with the city.
static inline void city_add_population_for(PolyState* s, int ci, int value, int score_player) {
    City* c = &s->cities[ci];
    if (c->population + value < -c->level) value = -c->level - c->population;
    c->population = (int16_t)(c->population + value);
    s->players[score_player].score += value * POINTS_PER_POPULATION;
    c->points_worth = (int16_t)(c->points_worth + value * POINTS_PER_POPULATION);
}
static inline void city_add_population(PolyState* s, int ci, int value) {
    city_add_population_for(s, ci, value, s->cities[ci].owner);
}

static inline void city_add_production(City* c, int prod) {
    c->production = (int16_t)(c->production + prod);
    if (c->production < 0) c->production = 0;
}

static inline bool city_can_add_unit(const City* c) {
    return c->num_units < c->level + 1 && c->num_units < CITY_MAX_UNITS;
}
static inline void city_add_unit(PolyState* s, int ci, int ui) {
    City* c = &s->cities[ci];
    if (city_can_add_unit(c)) { c->unit_ids[c->num_units++] = (int16_t)ui; }
    s->units[ui].city = (int8_t)ci;
}
static inline void city_remove_unit(PolyState* s, int ci, int ui) {
    City* c = &s->cities[ci];
    for (int i = 0; i < c->num_units; i++) {
        if (c->unit_ids[i] == ui) {
            for (int j = i; j < c->num_units - 1; j++) c->unit_ids[j] = c->unit_ids[j + 1];
            c->num_units--;
            return;
        }
    }
}

// Temple total points at its current level (Temple.getPoints)
static inline int temple_points_total(int level) {
    int p = 0;
    for (int i = 0; i < level; i++) p += TEMPLE_POINTS[i];
    return p;
}

// City.applyBonus — adjacency production/population pairs. Quirks kept:
//   - base placement adds its own bonus as population to its own city
//   - the paired building's bonus is credited to the PAIRED building's city
//   - an adjacent matching building in an ENEMY city aborts the whole scan
static inline void city_apply_bonus(PolyState* s, int ci, int tile, BuildingType type,
                                    bool is_population, bool only_matching, int multiplier) {
    City* c = &s->cities[ci];
    int player = c->owner;
    bool is_base = BUILDING_INFO[type].is_base;
    if (is_base && is_population && !only_matching)
        city_add_population(s, ci, multiplier * BUILDING_INFO[type].bonus);

    int n = s->size, x0 = tile % n, y0 = tile / n;
    BuildingType match = building_pair(type);
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        if (!dx && !dy) continue;
        int x = x0 + dx, y = y0 + dy;
        if (!in_bounds(s, x, y)) continue;
        int t = tile_idx(s, x, y);
        if (s->building[t] != match || match == BUILDING_NONE) continue;
        int oc = s->city_at[t];
        int add_to = ci;
        if (oc != ci) {
            if (oc < 0 || s->cities[oc].owner != player) return;  // Java: early abort
            add_to = oc;
        }
        int bonus = is_base ? BUILDING_INFO[match].bonus : BUILDING_INFO[type].bonus;
        if (is_population) city_add_population(s, add_to, bonus * multiplier);
        else city_add_production(&s->cities[add_to], bonus * multiplier);
    }
}

// City.updateBuildingEffects. `tile` holds the building; temple level read from planes.
// Quirk kept from City.java:180: on REMOVAL of a temple its accumulated points are
// ADDED (not subtracted) to the tribe score.
static inline void city_building_effects(PolyState* s, int ci, int tile, BuildingType type,
                                         bool negative, bool only_matching) {
    int mult = negative ? -1 : 1;
    switch (type) {
        case BUILDING_FARM: case BUILDING_LUMBER_HUT: case BUILDING_MINE:
        case BUILDING_WINDMILL: case BUILDING_SAWMILL: case BUILDING_FORGE:
            city_apply_bonus(s, ci, tile, type, true, only_matching, mult);
            break;
        case BUILDING_PORT:
            if (!only_matching) city_add_population(s, ci, PORT_BONUS_POP * mult);
            city_apply_bonus(s, ci, tile, type, false, only_matching, mult);
            break;
        case BUILDING_CUSTOMS_HOUSE:
            city_apply_bonus(s, ci, tile, type, false, only_matching, mult);
            break;
        case BUILDING_TEMPLE: case BUILDING_WATER_TEMPLE:
        case BUILDING_MOUNTAIN_TEMPLE: case BUILDING_FOREST_TEMPLE: {
            if (!only_matching)
                city_add_population(s, ci, BUILDING_INFO[type].bonus * mult);
            int score_diff = negative ? temple_points_total(s->temple_level[tile])
                                      : TEMPLE_POINTS[0];
            s->players[s->cities[ci].owner].score += score_diff;
            break;
        }
        default:  // monuments
            if (!only_matching)
                city_add_population(s, ci, BUILDING_INFO[type].bonus * mult);
            s->players[s->cities[ci].owner].score += MONUMENT_POINTS * mult;
            break;
    }
}

// ---------------------------------------------------------------------------
// Visibility (Tribe.clearView). v1 skips meetTribe / EYE_OF_GOD (partial-obs
// only mechanics; see docs/PLAN.md scope). Returns true if a road or water
// tile was revealed (triggers a trade-network recompute in Java).
// ---------------------------------------------------------------------------
static inline bool poly_clear_view(PolyState* s, int player, int x0, int y0, int range) {
    Player* p = &s->players[player];
    bool net_update = false;
    for (int dy = -range; dy <= range; dy++) for (int dx = -range; dx <= range; dx++) {
        int x = x0 + dx, y = y0 + dy;
        if (!in_bounds(s, x, y)) continue;
        int t = tile_idx(s, x, y);
        if (!obs_get(p, t)) {
            obs_set(p, t);
            p->score += CLEAR_VIEW_POINTS;
            p->tiles_seen++;
            if (poly_is_road(s, t) || terrain_is_water((Terrain)s->terrain[t]))
                net_update = true;
        }
    }
    return net_update;
}

// ---------------------------------------------------------------------------
// Trade network (TradeNetwork.java + Tribe.updateNetwork)
// ---------------------------------------------------------------------------
static inline bool player_controls_city(const PolyState* s, int player, int ci) {
    return ci >= 0 && s->cities[ci].owner == player;
}
static inline bool player_controls_capital(const PolyState* s, int player) {
    return player_controls_city(s, player, s->players[player].capital);
}

// BFS over navigable water: true if two ports are <= PORT_TRADE_DISTANCE apart.
static inline bool poly_ports_linked(const PolyState* s, const uint8_t* navigable,
                                     int from, int to) {
    int n = s->size, ntiles = n * n;
    int8_t dist[MAX_TILES];
    int16_t queue[MAX_TILES];
    for (int i = 0; i < ntiles; i++) dist[i] = -1;
    int head = 0, tail = 0;
    dist[from] = 0; queue[tail++] = (int16_t)from;
    while (head < tail) {
        int cur = queue[head++];
        if (cur == to) return true;
        if (dist[cur] >= PORT_TRADE_DISTANCE) continue;
        int cx = cur % n, cy = cur / n;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int x = cx + dx, y = cy + dy;
            if (!in_bounds(s, x, y)) continue;
            int t = tile_idx(s, x, y);
            if (!navigable[t] || dist[t] >= 0) continue;
            dist[t] = (int8_t)(dist[cur] + 1);
            queue[tail++] = (int16_t)t;
        }
    }
    return false;
}

// computeTradeNetworkTribe + Tribe.updateNetwork for one player.
// players_turn: newly connected non-capital cities gain +1 pop only on the
// owner's turn; capital gain/loss and disconnect penalties apply always.
static inline void poly_network_update_player(PolyState* s, int player, bool players_turn) {
    Player* p = &s->players[player];
    int n = s->size, ntiles = n * n;

    uint64_t was_connected = p->connected_cities;
    uint64_t now_connected = 0;
    int added = 0, lost = 0;

    if (player_controls_capital(s, player)) {
        // connectivity graph: own city centers & ports + own/neutral roads (all net tiles)
        uint8_t connected[MAX_TILES], navigable[MAX_TILES];
        int16_t ports[MAX_CITIES * 4]; int num_ports = 0;
        for (int t = 0; t < ntiles; t++) {
            connected[t] = 0; navigable[t] = 0;
            int ci = s->city_at[t];
            bool my_city = player_controls_city(s, player, ci);
            bool not_enemy = my_city || ci == -1;
            Terrain ter = (Terrain)s->terrain[t];
            bool port = s->building[t] == BUILDING_PORT;
            if (my_city && (ter == TERRAIN_CITY || port)) {
                connected[t] = s->net_tile[t];
                if (port && num_ports < (int)(sizeof(ports) / sizeof(ports[0])))
                    ports[num_ports++] = (int16_t)t;
            } else if (not_enemy && poly_is_road(s, t)) {
                connected[t] = s->net_tile[t];
            }
            if (terrain_is_water(ter) && obs_get(p, t) && not_enemy) navigable[t] = 1;
        }

        // port jump links (bidirectional)
        uint8_t linked[MAX_CITIES * 4][MAX_CITIES * 4 / 8];  // small bitset matrix
        memset(linked, 0, sizeof(linked));
        for (int i = 0; i < num_ports - 1; i++)
            for (int j = i + 1; j < num_ports; j++)
                if (poly_ports_linked(s, navigable, ports[i], ports[j])) {
                    linked[i][j >> 3] |= (uint8_t)(1u << (j & 7));
                    linked[j][i >> 3] |= (uint8_t)(1u << (i & 7));
                }

        // BFS from capital over connected tiles + jump links
        uint8_t seen[MAX_TILES]; int16_t queue[MAX_TILES + 64];
        memset(seen, 0, (size_t)ntiles);
        int head = 0, tail = 0;
        City* cap = &s->cities[p->capital];
        int cap_t = tile_idx(s, cap->x, cap->y);
        if (connected[cap_t]) { seen[cap_t] = 1; queue[tail++] = (int16_t)cap_t; }
        while (head < tail) {
            int cur = queue[head++];
            int cx = cur % n, cy = cur / n;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                int x = cx + dx, y = cy + dy;
                if (!in_bounds(s, x, y)) continue;
                int t = tile_idx(s, x, y);
                if (connected[t] && !seen[t]) { seen[t] = 1; queue[tail++] = (int16_t)t; }
            }
            for (int i = 0; i < num_ports; i++) {
                if (ports[i] != cur) continue;
                for (int j = 0; j < num_ports; j++)
                    if ((linked[i][j >> 3] >> (j & 7)) & 1u) {
                        int t = ports[j];
                        if (connected[t] && !seen[t]) { seen[t] = 1; queue[tail++] = (int16_t)t; }
                    }
            }
        }

        // connected cities = own non-capital cities whose center is reached
        for (int k = 0; k < p->num_cities; k++) {
            int ci = p->city_list[k];
            if (ci == p->capital) continue;
            City* c = &s->cities[ci];
            if (seen[tile_idx(s, c->x, c->y)]) now_connected |= 1ull << ci;
        }

        for (int ci = 0; ci < s->num_cities; ci++) {
            uint64_t bit = 1ull << ci;
            bool was = (was_connected & bit) != 0, is = (now_connected & bit) != 0;
            // includes formerly-connected cities we no longer own (bit clears);
            // score attribution follows THIS player, as in Java dropCityFromNetwork
            if (was && !is) { city_add_population_for(s, ci, -1, player); lost++; }
            else if (!was && is) { added++; }
        }
        p->connected_cities = now_connected;
        city_add_population_for(s, p->capital, added - lost, player);  // capital gain/loss

        int conn_count = 0;
        for (uint64_t b = now_connected; b; b >>= 1) conn_count += (int)(b & 1);
        if (conn_count >= GRAND_BAZAR_CITIES &&
            p->monuments[BUILDING_GRAND_BAZAR - 12] == MONUMENT_UNAVAILABLE)
            p->monuments[BUILDING_GRAND_BAZAR - 12] = MONUMENT_AVAILABLE;

        if (players_turn) {
            for (int ci = 0; ci < s->num_cities; ci++) {
                uint64_t bit = 1ull << ci;
                if (!(was_connected & bit) && (now_connected & bit))
                    city_add_population_for(s, ci, 1, player);
            }
        }
    } else {
        // no capital: everything disconnects (Tribe.updateNetwork first branch —
        // note Java does NOT apply the -1 pop penalty on this path)
        p->connected_cities = 0;
    }
}

// setTradeNetwork: flip a network tile and recompute for every player.
static inline void poly_set_net_tile(PolyState* s, int t, bool value) {
    s->net_tile[t] = value ? 1 : 0;
    for (int pl = 0; pl < s->num_players; pl++)
        poly_network_update_player(s, pl, pl == s->active_player);
}

// ---------------------------------------------------------------------------
// Unit add/remove/move (Board.addUnit/removeUnitFromBoard/moveUnit)
// ---------------------------------------------------------------------------
static inline int poly_new_unit(PolyState* s, UnitType type, int owner, int x, int y) {
    int i;
    for (i = 0; i < s->num_units; i++) if (s->units[i].type == UNIT_NONE) break;
    if (i == s->num_units) s->num_units++;
    Unit* u = &s->units[i];
    memset(u, 0, sizeof(*u));
    u->type = (int8_t)type; u->owner = (int8_t)owner; u->carried = UNIT_NONE;
    u->x = (int8_t)x; u->y = (int8_t)y;
    u->max_hp = UNIT_STATS[type].max_hp; u->hp = u->max_hp;
    u->status = STATUS_FINISHED;    // Java constructor default
    u->city = -1;
    s->unit_at[tile_idx(s, x, y)] = (int16_t)i;
    return i;
}

static inline void poly_remove_unit(PolyState* s, int ui) {
    Unit* u = &s->units[ui];
    if (u->city >= 0) city_remove_unit(s, u->city, ui);
    s->unit_at[tile_idx(s, u->x, u->y)] = -1;
    u->type = UNIT_NONE;
}

// Board.moveUnit: relocate + clear view (radius 1; +1 on mountain or battleship)
// + network recompute if new roads/water revealed.
static inline void poly_move_unit(PolyState* s, int ui, int x, int y) {
    Unit* u = &s->units[ui];
    s->unit_at[tile_idx(s, u->x, u->y)] = -1;
    s->unit_at[tile_idx(s, x, y)] = (int16_t)ui;
    u->x = (int8_t)x; u->y = (int8_t)y;
    int range = 1;
    if (s->terrain[tile_idx(s, x, y)] == TERRAIN_MOUNTAIN || u->type == UNIT_BATTLESHIP) range++;
    if (poly_clear_view(s, u->owner, x, y, range))
        poly_network_update_player(s, u->owner, u->owner == s->active_player);
}

// Board.embark/disembark — Java replaces the actor; we morph in place,
// preserving HP/kills/veteran/city and stashing the land type in `carried`.
// Java quirk kept: the replacement unit has constructor status FINISHED, and
// any pending status transition (MoveCommand's MOVED, pushUnit's PUSHED)
// lands on the detached old object — so the new unit stays FINISHED.
static inline void poly_embark(PolyState* s, int ui, int x, int y) {
    Unit* u = &s->units[ui];
    s->unit_at[tile_idx(s, u->x, u->y)] = -1;
    u->carried = u->type;
    u->type = UNIT_BOAT;
    u->status = STATUS_FINISHED;
    u->x = (int8_t)x; u->y = (int8_t)y;
    s->unit_at[tile_idx(s, x, y)] = (int16_t)ui;
}
static inline void poly_disembark(PolyState* s, int ui, int x, int y) {
    Unit* u = &s->units[ui];
    s->unit_at[tile_idx(s, u->x, u->y)] = -1;
    u->type = u->carried >= 0 ? u->carried : UNIT_WARRIOR;  // Java fallback
    u->carried = UNIT_NONE;
    u->status = STATUS_FINISHED;
    u->x = (int8_t)x; u->y = (int8_t)y;
    s->unit_at[tile_idx(s, x, y)] = (int16_t)ui;
}

// ---------------------------------------------------------------------------
// Push (Board.pushUnit / tryPush). Push-order arrays verbatim from
// Board.java:295-296. NOTE Java's Vector2d "x" is the ROW index, so xPush
// applies to our y and yPush to our x (verified against golden traces:
// first candidate is col+1, not row+1).
// ---------------------------------------------------------------------------
static const int8_t PUSH_DY[8] = {0, -1, 0, 1, -1, -1, 1, 1};   // Java xPush (rows)
static const int8_t PUSH_DX[8] = {1, 0, -1, 0, 1, -1, -1, 1};   // Java yPush (cols)

static inline bool poly_try_push(PolyState* s, int ui, int x, int y) {
    Unit* u = &s->units[ui];
    int t = tile_idx(s, x, y);
    if (s->unit_at[t] >= 0) return false;
    Terrain ter = (Terrain)s->terrain[t];
    if (ter == TERRAIN_MOUNTAIN) {
        if (!tech_researched(&s->players[u->owner], TECH_CLIMBING)) return false;
        poly_move_unit(s, ui, x, y);
        return true;
    }
    if (terrain_is_water(ter)) {
        if (UNIT_STATS[u->type].water) return true;   // Java quirk: "pushed" without moving
        if (s->building[t] == BUILDING_PORT) {
            int ci = s->city_at[t];
            if (ci >= 0 && s->cities[ci].owner == u->owner) { poly_embark(s, ui, x, y); return true; }
        }
        return false;
    }
    poly_move_unit(s, ui, x, y);
    return true;
}

// Returns false if the unit could not be pushed anywhere (Java: it vanishes —
// caller must then remove it). Sets PUSHED, except when the push embarked the
// unit (Java's PUSHED lands on the stale pre-embark object; see poly_embark).
static inline bool poly_push_unit(PolyState* s, int ui) {
    Unit* u = &s->units[ui];
    int sx = u->x, sy = u->y;
    bool was_water = UNIT_STATS[u->type].water;
    bool pushed = false;
    for (int i = 0; i < 8 && !pushed; i++) {
        int x = sx + PUSH_DX[i], y = sy + PUSH_DY[i];
        if (in_bounds(s, x, y)) pushed = poly_try_push(s, ui, x, y);
    }
    if (was_water || !UNIT_STATS[u->type].water)   // no embark happened
        u->status = STATUS_PUSHED;
    return pushed;
}

// ---------------------------------------------------------------------------
// Capture bookkeeping (Board.capture + moveOne/moveAll/moveLast + Tribe
// capturedCity/lostCity/manageLoss). Java's seeded Collections.shuffle over
// the tribe's non-capital city list is reproduced (fisher-yates, same order).
// ---------------------------------------------------------------------------
static inline void poly_shuffle_cities(uint32_t* rng, int8_t* a, int n) {
    for (int i = n - 1; i >= 1; i--) {
        int j = poly_rand_int(rng, i + 1);
        int8_t tmp = a[i]; a[i] = a[j]; a[j] = tmp;
    }
}

static inline void poly_move_last_unit(PolyState* s, int from_ci, int to_ci) {
    City* from = &s->cities[from_ci];
    if (from->num_units == 0) return;
    int ui = from->unit_ids[--from->num_units];
    City* to = &s->cities[to_ci];
    if (to->num_units < CITY_MAX_UNITS) to->unit_ids[to->num_units++] = (int16_t)ui;
    s->units[ui].city = (int8_t)to_ci;
}

// Board.moveOneToNewCity: dest city inherits one unit-slot association,
// preferring the capital as donor, else a random owned city with units.
static inline void poly_move_one_to_new_city(PolyState* s, int dest_ci, int player) {
    Player* p = &s->players[player];
    int8_t others[MAX_CITIES]; int n_others = 0;
    for (int k = 0; k < p->num_cities; k++)
        if (p->city_list[k] != p->capital) others[n_others++] = p->city_list[k];

    if (player_controls_capital(s, player) && s->cities[p->capital].num_units > 0) {
        poly_move_last_unit(s, p->capital, dest_ci);
        return;
    }
    poly_shuffle_cities(&s->rng, others, n_others);
    for (int k = 0; k < n_others; k++) {
        // Java iterates the shuffled list without excluding dest; a self-move
        // is a no-op that still terminates the search — keep that behavior.
        if (s->cities[others[k]].num_units > 0) {
            poly_move_last_unit(s, others[k], dest_ci);
            return;
        }
    }
}

// Board.moveAllFromCity: units of a lost city are re-homed: capital first,
// then random own cities, remainder become tribe-owned "extra" units.
static inline void poly_move_all_from_city(PolyState* s, int from_ci, int player) {
    Player* p = &s->players[player];
    City* from = &s->cities[from_ci];
    if (player_controls_capital(s, player)) {
        while (city_can_add_unit(&s->cities[p->capital]) && from->num_units > 0)
            poly_move_last_unit(s, from_ci, p->capital);
    }
    if (from->num_units > 0) {
        int8_t others[MAX_CITIES]; int n_others = 0;
        for (int k = 0; k < p->num_cities; k++)
            if (p->city_list[k] != p->capital) others[n_others++] = p->city_list[k];
        poly_shuffle_cities(&s->rng, others, n_others);
        for (int k = 0; k < n_others && from->num_units > 0; k++) {
            while (city_can_add_unit(&s->cities[others[k]]) && from->num_units > 0)
                poly_move_last_unit(s, from_ci, others[k]);
        }
        while (from->num_units > 0) {           // remainder: tribe-owned
            int ui = from->unit_ids[--from->num_units];
            s->units[ui].city = -1;
        }
    }
}

static inline void player_add_city(PolyState* s, int player, int ci) {
    Player* p = &s->players[player];
    p->city_list[p->num_cities++] = (int8_t)ci;
    s->cities[ci].owner = (int8_t)player;
}
static inline void player_remove_city(PolyState* s, int player, int ci) {
    Player* p = &s->players[player];
    for (int k = 0; k < p->num_cities; k++) {
        if (p->city_list[k] == ci) {
            for (int j = k; j < p->num_cities - 1; j++) p->city_list[j] = p->city_list[j + 1];
            p->num_cities--;
            return;
        }
    }
}

// Board.assignCityTiles: claim unowned tiles in the city's bound radius.
static inline void poly_assign_city_tiles(PolyState* s, int ci, int radius) {
    City* c = &s->cities[ci];
    for (int dy = -radius; dy <= radius; dy++) for (int dx = -radius; dx <= radius; dx++) {
        int x = c->x + dx, y = c->y + dy;
        if (!in_bounds(s, x, y)) continue;
        int t = tile_idx(s, x, y);
        if (s->city_at[t] == -1) {
            s->city_at[t] = (int8_t)ci;
            s->players[c->owner].score += CITY_BORDER_POINTS;
            c->points_worth = (int16_t)(c->points_worth + CITY_BORDER_POINTS);
        }
    }
}

// Tribe.manageLoss: player is out; all their units are removed from the board.
static inline void poly_manage_loss(PolyState* s, int player) {
    s->players[player].result = RESULT_LOSS;
    for (int ui = 0; ui < s->num_units; ui++)
        if (s->units[ui].type != UNIT_NONE && s->units[ui].owner == player)
            poly_remove_unit(s, ui);
}

// Board.capture: village -> new city, or enemy city -> ownership transfer.
// Returns the captured/created city index, or -1 on failure.
static inline int poly_capture(PolyState* s, int player, int x, int y) {
    int t = tile_idx(s, x, y);
    Terrain ter = (Terrain)s->terrain[t];

    if (ter == TERRAIN_VILLAGE) {
        int ci = s->num_cities++;
        City* c = &s->cities[ci];
        memset(c, 0, sizeof(*c));
        c->x = (int8_t)x; c->y = (int8_t)y;
        c->level = 1; c->population_need = 2; c->bound = 1; c->owner = -1;
        // Java order: addCityToTribe (addCity, clearView, setTradeNetwork —
        // note the FIRST network recompute runs while the tile is still
        // VILLAGE terrain and unowned) → assignCityTiles → terrain=CITY →
        // moveOneToNewCity → centre points → setTradeNetwork AGAIN.
        player_add_city(s, player, ci);
        poly_clear_view(s, player, x, y, NEW_CITY_CLEAR_RANGE);  // return discarded, as in Java
        poly_set_net_tile(s, t, true);                            // recompute #1
        poly_assign_city_tiles(s, ci, c->bound);
        s->terrain[t] = TERRAIN_CITY;
        poly_move_one_to_new_city(s, ci, player);
        s->players[player].score += CITY_CENTRE_POINTS;
        poly_set_net_tile(s, t, true);                            // recompute #2
        return ci;
    }

    if (ter == TERRAIN_CITY) {
        int ci = s->city_at[t];
        City* c = &s->cities[ci];
        int prev_owner = c->owner;

        // Tribe.capturedCity: transfer + building effects for the new owner
        player_add_city(s, player, ci);      // sets c->owner = player
        player_remove_city(s, prev_owner, ci);
        if (c->is_capital) {                 // reward accounting
            s->players[player].capitals_taken++;
            s->players[prev_owner].capitals_lost++;
        }
        for (int bt = 0; bt < s->size * s->size; bt++)
            if (s->city_at[bt] == ci && s->building[bt] != BUILDING_NONE)
                city_building_effects(s, ci, bt, (BuildingType)s->building[bt], false, true);
        // Tribe.lostCity: base/port building effects for the previous owner —
        // note owner already flipped in Java too (capturedCity runs first)
        for (int bt = 0; bt < s->size * s->size; bt++)
            if (s->city_at[bt] == ci && s->building[bt] != BUILDING_NONE) {
                BuildingType b = (BuildingType)s->building[bt];
                if (BUILDING_INFO[b].is_base || b == BUILDING_PORT)
                    city_building_effects(s, ci, bt, b, true, true);
            }

        poly_move_all_from_city(s, ci, prev_owner);
        poly_move_one_to_new_city(s, ci, player);

        if (s->players[prev_owner].num_cities == 0)
            poly_manage_loss(s, prev_owner);

        poly_set_net_tile(s, t, true);
        return ci;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Engine API surface (see actions.h for actions, game.h for the turn machine,
// mapgen.h for procedural generation / poly_reset)
// ---------------------------------------------------------------------------

// True when the game has ended; results are in players[i].result.
static inline bool poly_done(const PolyState* s) { return s->game_over; }

#endif // POLYTOPIA_H
