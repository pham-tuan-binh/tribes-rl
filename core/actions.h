// polytopia-rl — action feasibility, execution and enumeration.
// Exact port of src/core/actions/** (isFeasible + *Command.execute) minus
// diplomacy (DeclareWar/SendStars — cut in v1, see docs/PLAN.md).
#ifndef POLY_ACTIONS_H
#define POLY_ACTIONS_H

#include "polytopia.h"

typedef enum {
    ACT_END_TURN = 0,
    ACT_RESEARCH,       // arg = Tech
    ACT_BUILD_ROAD,     // target = tile
    ACT_MOVE,           // actor = unit, target = tile
    ACT_ATTACK,         // actor = unit, target = unit
    ACT_CAPTURE,        // actor = unit (tile = unit pos)
    ACT_RECOVER,        // actor = unit
    ACT_HEAL_OTHERS,    // actor = unit
    ACT_CONVERT,        // actor = unit, target = unit
    ACT_MAKE_VETERAN,   // actor = unit
    ACT_UPGRADE,        // actor = unit (boat->ship / ship->battleship)
    ACT_DISBAND,        // actor = unit
    ACT_EXAMINE,        // actor = unit (ruins; bonus rolled at execute)
    ACT_BUILD,          // actor = city, target = tile, arg = BuildingType
    ACT_SPAWN,          // actor = city, arg = UnitType
    ACT_LEVELUP,        // actor = city, arg = LevelUpBonus
    ACT_GATHER,         // actor = city, target = tile
    ACT_CLEAR_FOREST,   // actor = city, target = tile
    ACT_BURN_FOREST,    // actor = city, target = tile
    ACT_GROW_FOREST,    // actor = city, target = tile
    ACT_DESTROY,        // actor = city, target = tile
    NUM_ACT_KINDS
} ActKind;

typedef struct {
    int8_t kind;
    int16_t actor;   // unit or city index (kind-dependent), -1 if n/a
    int16_t target;  // tile index or unit index (kind-dependent), -1 if n/a
    int8_t arg;      // building/unit-type/tech/levelup bonus, -1 if n/a
} PolyAction;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// GameState.killUnit: off the board, out of its city, minus its points.
static inline void poly_kill_unit(PolyState* s, int ui) {
    Unit* u = &s->units[ui];
    s->players[u->owner].score -= UNIT_STATS[u->type].points;
    s->players[u->owner].lost_value += UNIT_STATS[u->type].cost;
    poly_remove_unit(s, ui);
}

static inline double attack_def_bonus(const PolyState* s, const Unit* target) {
    int t = tile_idx(s, target->x, target->y);
    Terrain ter = (Terrain)s->terrain[t];
    int ci = s->city_at[t];
    bool own_city = ter == TERRAIN_CITY && ci >= 0 && s->cities[ci].owner == target->owner;
    return combat_def_bonus(ter, own_city, own_city && s->cities[ci].has_walls,
                            UNIT_STATS[target->type].fortify, &s->players[target->owner]);
}

static inline bool player_has_levelup_pending(const PolyState* s, int player) {
    for (int k = 0; k < s->players[player].num_cities; k++) {
        const City* c = &s->cities[s->players[player].city_list[k]];
        if (c->population >= c->population_need) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Feasibility (isFeasible ports)
// ---------------------------------------------------------------------------

static inline bool feas_move(PolyState* s, int ui, int dest, const uint8_t* dest_ok) {
    const Unit* u = &s->units[ui];
    return unit_can_move(u) && dest_ok[dest];
}

static inline bool feas_attack(const PolyState* s, int ui, int ti) {
    const Unit* u = &s->units[ui], *t = &s->units[ti];
    if (u->type == UNIT_MIND_BENDER || !unit_can_attack(u)) return false;
    if (t->type == UNIT_NONE || t->owner == u->owner) return false;
    if (!obs_get(&s->players[u->owner], tile_idx(s, t->x, t->y))) return false;
    return chebyshev(u->x, u->y, t->x, t->y) <= UNIT_STATS[u->type].range;
}

static inline bool feas_capture(const PolyState* s, int ui) {
    const Unit* u = &s->units[ui];
    if (u->status != STATUS_FRESH) return false;
    int t = tile_idx(s, u->x, u->y);
    Terrain ter = (Terrain)s->terrain[t];
    if (ter == TERRAIN_VILLAGE) return true;
    if (ter == TERRAIN_CITY) {
        int ci = s->city_at[t];
        return ci >= 0 && s->cities[ci].owner != u->owner;
    }
    return false;
}

static inline bool feas_recover(const PolyState* s, int ui) {
    const Unit* u = &s->units[ui];
    return u->status == STATUS_FRESH && u->hp > 0 && u->hp < u->max_hp;
}

static inline bool feas_convert(const PolyState* s, int ui, int ti) {
    const Unit* u = &s->units[ui], *t = &s->units[ti];
    if (u->type != UNIT_MIND_BENDER || !unit_can_attack(u)) return false;
    if (t->type == UNIT_NONE || t->owner == u->owner) return false;
    if (!obs_get(&s->players[u->owner], tile_idx(s, t->x, t->y))) return false;
    return chebyshev(u->x, u->y, t->x, t->y) <= UNIT_STATS[u->type].range;
}

static inline bool feas_heal_others(const PolyState* s, int ui) {
    const Unit* u = &s->units[ui];
    if (u->type != UNIT_MIND_BENDER || !unit_can_attack(u)) return false;
    int r = UNIT_STATS[u->type].range;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        if (!dx && !dy) continue;
        int x = u->x + dx, y = u->y + dy;
        if (!in_bounds(s, x, y)) continue;
        int16_t oi = s->unit_at[tile_idx(s, x, y)];
        if (oi >= 0 && s->units[oi].owner == u->owner && s->units[oi].hp < s->units[oi].max_hp)
            return true;
    }
    return false;
}

static inline bool feas_make_veteran(const PolyState* s, int ui) {
    const Unit* u = &s->units[ui];
    return u->type != UNIT_SUPERUNIT && !u->veteran && u->kills >= VETERAN_KILLS;
}

static inline bool feas_upgrade(const PolyState* s, int ui) {
    const Unit* u = &s->units[ui];
    const Player* p = &s->players[u->owner];
    if (u->type == UNIT_BOAT)
        return tech_researched(p, TECH_SAILING) && p->stars >= UNIT_STATS[UNIT_SHIP].cost;
    if (u->type == UNIT_SHIP)
        return tech_researched(p, TECH_NAVIGATION) && p->stars >= UNIT_STATS[UNIT_BATTLESHIP].cost;
    return false;
}

static inline bool feas_disband(const PolyState* s, int ui) {
    return s->units[ui].status == STATUS_FRESH &&
           tech_researched(&s->players[s->units[ui].owner], TECH_FREE_SPIRIT);
}

static inline bool feas_examine(const PolyState* s, int ui) {
    const Unit* u = &s->units[ui];
    return u->status == STATUS_FRESH && s->players[u->owner].num_cities >= 1 &&
           s->resource[tile_idx(s, u->x, u->y)] == RESOURCE_RUINS;
}

static inline bool feas_build(const PolyState* s, int ci, int tile, BuildingType b) {
    const City* c = &s->cities[ci];
    const Player* p = &s->players[c->owner];
    const BuildingInfo* bi = &BUILDING_INFO[b];
    if (s->city_at[tile] != ci || s->building[tile] != BUILDING_NONE) return false;
    if (p->stars < bi->cost) return false;
    if (bi->tech_req != TECH_NONE && !tech_researched(p, (Tech)bi->tech_req)) return false;
    if (!((bi->terrain_mask >> s->terrain[tile]) & 1u)) return false;
    if (bi->resource_req != RESOURCE_NONE && s->resource[tile] != bi->resource_req) return false;
    if (bi->is_monument && p->monuments[b - 12] != MONUMENT_AVAILABLE) return false;
    if (bi->adj_req != BUILDING_NONE) {
        bool found = false;
        int n = s->size, x0 = tile % n, y0 = tile / n;
        for (int dy = -1; dy <= 1 && !found; dy++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int x = x0 + dx, y = y0 + dy;
            if (in_bounds(s, x, y) && s->building[tile_idx(s, x, y)] == bi->adj_req) { found = true; break; }
        }
        if (!found) return false;
    }
    // Uniqueness (Build.isFeasible): SAWMILL/CUSTOMS_HOUSE/WINDMILL/FORGE are
    // one-per-city. PORT pairs with CUSTOMS_HOUSE but is REPEATABLE in Java.
    if (b == BUILDING_SAWMILL || b == BUILDING_CUSTOMS_HOUSE ||
        b == BUILDING_WINDMILL || b == BUILDING_FORGE) {
        int ntiles = s->size * s->size;
        for (int t = 0; t < ntiles; t++)
            if (s->city_at[t] == ci && s->building[t] == b) return false;
    }
    return true;
}

static inline bool feas_spawn(const PolyState* s, int ci, UnitType ut) {
    const City* c = &s->cities[ci];
    const Player* p = &s->players[c->owner];
    if (!UNIT_STATS[ut].spawnable) return false;
    if (p->stars < UNIT_STATS[ut].cost) return false;
    if (!city_can_add_unit(c)) return false;
    if (s->unit_at[tile_idx(s, c->x, c->y)] >= 0) return false;
    int8_t tech = UNIT_STATS[ut].tech_req;
    return tech == TECH_NONE || tech_researched(p, (Tech)tech);
}

static inline bool levelup_valid_for_level(LevelUpBonus b, int level) {
    switch (b) {
        case LEVELUP_WORKSHOP: case LEVELUP_EXPLORER:        return level == 1;
        case LEVELUP_CITY_WALL: case LEVELUP_RESOURCES:      return level == 2;
        case LEVELUP_POP_GROWTH: case LEVELUP_BORDER_GROWTH: return level == 3;
        case LEVELUP_PARK: case LEVELUP_SUPERUNIT:           return level >= 4;
        default: return false;
    }
}
static inline bool feas_levelup(const PolyState* s, int ci, LevelUpBonus b) {
    const City* c = &s->cities[ci];
    return c->population >= c->population_need && levelup_valid_for_level(b, c->level);
}

static inline bool feas_gather(const PolyState* s, int ci, int tile) {
    Resource r = (Resource)s->resource[tile];
    if (r == RESOURCE_NONE || r == RESOURCE_RUINS || r == RESOURCE_ORE || r == RESOURCE_CROPS)
        return false;  // Java ResourceGathering covers FISH/FRUIT/ANIMAL/WHALES
    if (s->city_at[tile] != ci) return false;
    const Player* p = &s->players[s->cities[ci].owner];
    if (p->stars < RESOURCE_INFO[r].cost) return false;
    return tech_researched(p, (Tech)RESOURCE_INFO[r].tech_req);
}

static inline bool feas_forest_op(const PolyState* s, int ci, int tile, ActKind kind) {
    const Player* p = &s->players[s->cities[ci].owner];
    if (s->city_at[tile] != ci) return false;
    switch (kind) {
        case ACT_CLEAR_FOREST:
            return s->terrain[tile] == TERRAIN_FOREST && tech_researched(p, TECH_FORESTRY);
        case ACT_BURN_FOREST:
            return s->terrain[tile] == TERRAIN_FOREST && p->stars >= BURN_FOREST_COST &&
                   tech_researched(p, TECH_CHIVALRY);
        case ACT_GROW_FOREST:
            return s->terrain[tile] == TERRAIN_PLAIN && p->stars >= GROW_FOREST_COST &&
                   tech_researched(p, TECH_SPIRITUALISM);
        default: return false;
    }
}

static inline bool feas_destroy(const PolyState* s, int ci, int tile) {
    return s->city_at[tile] == ci && s->building[tile] != BUILDING_NONE &&
           tech_researched(&s->players[s->cities[ci].owner], TECH_CONSTRUCTION);
}

static inline bool feas_research(const PolyState* s, int player, Tech t) {
    const Player* p = &s->players[player];
    return tech_researchable(p, t) &&
           p->stars >= tech_cost(t, p->num_cities, tech_researched(p, TECH_PHILOSOPHY));
}

static inline bool feas_build_road(const PolyState* s, int player, int tile) {
    const Player* p = &s->players[player];
    if (!tech_researched(p, TECH_ROADS) || p->stars < ROAD_COST) return false;
    if (!obs_get(p, tile)) return false;
    Terrain ter = (Terrain)s->terrain[tile];
    if (s->tribes_compat) {   // Tribes: village/plain/forest
        if (ter != TERRAIN_VILLAGE && ter != TERRAIN_PLAIN && ter != TERRAIN_FOREST) return false;
    } else {                  // product rules: open fields only (as real Polytopia)
        if (ter != TERRAIN_PLAIN) return false;
    }
    int ci = s->city_at[tile];
    if (!(ci == -1 || s->cities[ci].owner == player)) return false;
    if (s->net_tile[tile]) return false;
    int16_t ui = s->unit_at[tile];
    return !(ui >= 0 && s->units[ui].owner != player);
}

// ---------------------------------------------------------------------------
// Execution (*Command.execute ports). All return false if infeasible.
// ---------------------------------------------------------------------------

static inline bool exec_move(PolyState* s, int ui, int dest) {
    uint8_t dest_ok[MAX_TILES];
    poly_reachable(s, ui, dest_ok);
    if (!feas_move(s, ui, dest, dest_ok)) return false;
    Unit* u = &s->units[ui];
    int x = dest % s->size, y = dest / s->size;
    Terrain dest_ter = (Terrain)s->terrain[dest];
    poly_move_unit(s, ui, x, y);
    bool morphed = false;   // embark/disembark replace the Java actor: the
    if (UNIT_STATS[u->type].water) {                 // MOVED transition below
        if (!terrain_is_water(dest_ter)) { poly_disembark(s, ui, x, y); morphed = true; }
    } else if (s->building[dest] == BUILDING_PORT) { // hits the stale object,
        poly_embark(s, ui, x, y); morphed = true;    // leaving the new unit
    }                                                // FINISHED
    if (!morphed) unit_transition(u, STATUS_MOVED);
    return true;
}

static inline bool exec_attack(PolyState* s, int ui, int ti) {
    if (!feas_attack(s, ui, ti)) return false;
    Unit* atk = &s->units[ui];
    Unit* def = &s->units[ti];
    unit_transition(atk, STATUS_ATTACKED);
    s->players[atk->owner].pacifist_count = 0;

    CombatResult r = combat_results(UNIT_STATS[atk->type].atk, atk->hp, atk->max_hp,
                                    UNIT_STATS[def->type].def, def->hp, def->max_hp,
                                    attack_def_bonus(s, def));
    if (def->hp <= r.to_target) {
        unit_add_kill(atk);
        s->players[atk->owner].num_kills++;
        s->players[atk->owner].kill_value += UNIT_STATS[def->type].cost;
        int tx = def->x, ty = def->y;
        poly_kill_unit(s, ti);
        if (UNIT_STATS[atk->type].melee_push)
            poly_try_push(s, ui, tx, ty);   // melee advance; failure = stay put
    } else {
        def->hp = (int16_t)(def->hp - r.to_target);
        if (chebyshev(atk->x, atk->y, def->x, def->y) <= UNIT_STATS[def->type].range) {
            atk->hp = (int16_t)(atk->hp - r.to_attacker);
            if (atk->hp <= 0) {
                unit_add_kill(def);
                s->players[def->owner].num_kills++;
                s->players[def->owner].kill_value += UNIT_STATS[atk->type].cost;
                poly_kill_unit(s, ui);
            }
        }
    }
    return true;
}

static inline bool exec_capture(PolyState* s, int ui) {
    if (!feas_capture(s, ui)) return false;
    Unit* u = &s->units[ui];
    int t = tile_idx(s, u->x, u->y);
    if (s->terrain[t] == TERRAIN_CITY) {
        int ci = s->city_at[t];
        City* c = &s->cities[ci];
        s->players[c->owner].score -= c->points_worth;
        s->players[u->owner].score += c->points_worth;
        u->status = STATUS_FINISHED;
        return poly_capture(s, u->owner, u->x, u->y) >= 0;
    }
    u->status = STATUS_FINISHED;
    return poly_capture(s, u->owner, u->x, u->y) >= 0;
}

static inline bool exec_recover(PolyState* s, int ui) {
    if (!feas_recover(s, ui)) return false;
    Unit* u = &s->units[ui];
    int add = RECOVER_PLUS_HP;
    int ci = s->city_at[tile_idx(s, u->x, u->y)];
    if (ci >= 0 && s->cities[ci].owner == u->owner) add += RECOVER_IN_BORDERS_PLUS_HP;
    u->hp = (int16_t)(u->hp + add > u->max_hp ? u->max_hp : u->hp + add);
    unit_transition(u, STATUS_FINISHED);
    return true;
}

static inline bool exec_heal_others(PolyState* s, int ui) {
    if (!feas_heal_others(s, ui)) return false;
    Unit* u = &s->units[ui];
    int r = UNIT_STATS[u->type].range;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        if (!dx && !dy) continue;
        int x = u->x + dx, y = u->y + dy;
        if (!in_bounds(s, x, y)) continue;
        int16_t oi = s->unit_at[tile_idx(s, x, y)];
        if (oi >= 0 && s->units[oi].owner == u->owner) {
            Unit* o = &s->units[oi];
            o->hp = (int16_t)(o->hp + MINDBENDER_HEAL > o->max_hp ? o->max_hp : o->hp + MINDBENDER_HEAL);
        }
    }
    unit_transition(u, STATUS_ATTACKED);
    return true;
}

static inline bool exec_convert(PolyState* s, int ui, int ti) {
    if (!feas_convert(s, ui, ti)) return false;
    Unit* u = &s->units[ui];
    Unit* t = &s->units[ti];
    if (t->city >= 0) city_remove_unit(s, t->city, ti);
    t->city = -1;                     // becomes converter tribe's "extra" unit
    t->owner = u->owner;
    unit_transition(u, STATUS_ATTACKED);
    t->status = STATUS_FINISHED;
    return true;
}

static inline bool exec_make_veteran(PolyState* s, int ui) {
    if (!feas_make_veteran(s, ui)) return false;
    Unit* u = &s->units[ui];
    u->veteran = true;
    u->max_hp = (int16_t)(u->max_hp + VETERAN_PLUS_HP);
    u->hp = u->max_hp;
    return true;
}

static inline bool exec_upgrade(PolyState* s, int ui) {
    if (!feas_upgrade(s, ui)) return false;
    Unit* u = &s->units[ui];
    UnitType next = u->type == UNIT_BOAT ? UNIT_SHIP : UNIT_BATTLESHIP;
    s->players[u->owner].stars -= UNIT_STATS[next].cost;
    u->type = (int8_t)next;   // hp/kills/veteran/status/carried/city preserved
    return true;
}

static inline bool exec_disband(PolyState* s, int ui) {
    if (!feas_disband(s, ui)) return false;
    Unit* u = &s->units[ui];
    s->players[u->owner].stars += UNIT_STATS[u->type].cost / 2;
    poly_kill_unit(s, ui);   // removal + score subtraction, as DisbandCommand
    return true;
}

static inline bool exec_examine(PolyState* s, int ui) {
    if (!feas_examine(s, ui)) return false;
    Unit* u = &s->units[ui];
    Player* p = &s->players[u->owner];
    int player = u->owner;
    int tile = tile_idx(s, u->x, u->y);

    int handler = p->city_list[0];
    if (player_controls_capital(s, player)) handler = p->capital;

    bool all_tech = (p->techs & ((1u << NUM_TECH) - 1)) == ((1u << NUM_TECH) - 1);
    int bonus = poly_rand_int(&s->rng, NUM_EXAMINE);
    while (all_tech && bonus == EXAMINE_RESEARCH) bonus = poly_rand_int(&s->rng, NUM_EXAMINE);

    switch (bonus) {
        case EXAMINE_SUPERUNIT: {
            Terrain ter = (Terrain)s->terrain[tile];
            UnitType ut = terrain_is_water(ter) ? UNIT_BATTLESHIP : UNIT_SUPERUNIT;
            // the examining unit itself is pushed away (killed if no room)
            if (!poly_push_unit(s, ui)) poly_kill_unit(s, ui);
            int x = tile % s->size, y = tile / s->size;
            int ni = poly_new_unit(s, ut, player, x, y);
            if (ut == UNIT_BATTLESHIP) s->units[ni].carried = UNIT_WARRIOR;
            s->units[ni].city = -1;              // Java creates with cityId -1 (extra unit)
            s->resource[tile] = RESOURCE_NONE;
            break;
        }
        case EXAMINE_RESEARCH: {
            // researchAtRandom: uniform over researchable techs, no score awarded
            int8_t avail[NUM_TECH]; int n_avail = 0;
            for (int t = 0; t < NUM_TECH; t++)
                if (tech_researchable(p, (Tech)t)) avail[n_avail++] = (int8_t)t;
            if (n_avail > 0) {
                Tech t = (Tech)avail[poly_rand_int(&s->rng, n_avail)];
                p->techs |= 1u << t;
                if ((p->techs & ((1u << NUM_TECH) - 1)) == ((1u << NUM_TECH) - 1) &&
                    p->monuments[BUILDING_TOWER_OF_WISDOM - 12] == MONUMENT_UNAVAILABLE)
                    p->monuments[BUILDING_TOWER_OF_WISDOM - 12] = MONUMENT_AVAILABLE;
            }
            break;
        }
        case EXAMINE_POP_GROWTH:
            city_add_population_for(s, handler, EXAMINE_POP_GROWTH_AMOUNT, player);
            break;
        case EXAMINE_EXPLORER: {
            // Board.launchExplorer: 15 random traversable steps clearing view r=1
            int cx = u->x, cy = u->y;
            for (int step = 0; step < EXPLORER_NUM_STEPS; step++) {
                bool moved = false;
                for (int tries = 0; tries < EXPLORER_NUM_STEPS * 3 && !moved; tries++) {
                    int dir = poly_rand_int(&s->rng, 8);
                    int x = cx + PUSH_DX[dir], y = cy + PUSH_DY[dir];
                    if (!in_bounds(s, x, y)) continue;
                    if (poly_traversable(s, x, y, player)) {
                        moved = true;
                        cx = x; cy = y;
                        if (poly_clear_view(s, player, cx, cy, EXPLORER_CLEAR_RANGE))
                            poly_network_update_player(s, player, player == s->active_player);
                    }
                }
            }
            break;
        }
        case EXAMINE_RESOURCES:
            p->stars += EXAMINE_RESOURCES_STARS;
            break;
    }
    if (bonus != EXAMINE_SUPERUNIT) s->resource[tile] = RESOURCE_NONE;
    if (s->units[ui].type != UNIT_NONE) s->units[ui].status = STATUS_FINISHED;
    return true;
}

static inline bool exec_build(PolyState* s, int ci, int tile, BuildingType b) {
    if (!feas_build(s, ci, tile, b)) return false;
    City* c = &s->cities[ci];
    s->players[c->owner].stars -= BUILDING_INFO[b].cost;
    s->building[tile] = (int8_t)b;
    s->resource[tile] = RESOURCE_NONE;
    if (BUILDING_INFO[b].is_temple) { s->temple_level[tile] = 1; s->temple_turns[tile] = TEMPLE_TURNS_TO_SCORE; }
    city_building_effects(s, ci, tile, b, false, false);
    if (b == BUILDING_PORT) poly_set_net_tile(s, tile, true);
    if (BUILDING_INFO[b].is_monument) s->players[c->owner].monuments[b - 12] = MONUMENT_BUILT;
    if (b == BUILDING_LUMBER_HUT) s->terrain[tile] = TERRAIN_PLAIN;
    return true;
}

static inline bool exec_spawn(PolyState* s, int ci, UnitType ut) {
    if (!feas_spawn(s, ci, ut)) return false;
    City* c = &s->cities[ci];
    int ui = poly_new_unit(s, ut, c->owner, c->x, c->y);
    city_add_unit(s, ci, ui);
    s->players[c->owner].stars -= UNIT_STATS[ut].cost;
    s->players[c->owner].score += UNIT_STATS[ut].points;
    return true;
}

static inline bool exec_levelup(PolyState* s, int ci, LevelUpBonus b) {
    if (!feas_levelup(s, ci, b)) return false;
    City* c = &s->cities[ci];
    Player* p = &s->players[c->owner];
    if (b == LEVELUP_PARK || b == LEVELUP_SUPERUNIT) {          // grantsMonument
        if (p->monuments[BUILDING_PARK_OF_FORTUNE - 12] == MONUMENT_UNAVAILABLE)
            p->monuments[BUILDING_PARK_OF_FORTUNE - 12] = MONUMENT_AVAILABLE;
    }
    p->score += LEVELUP_POINTS[b];
    c->points_worth = (int16_t)(c->points_worth + LEVELUP_POINTS[b]);
    c->level++;
    c->population = (int16_t)(c->population - c->population_need);
    c->population_need = (int16_t)(c->level + 1);

    switch (b) {
        case LEVELUP_WORKSHOP: city_add_production(c, CITY_LEVEL_UP_WORKSHOP_PROD); break;
        case LEVELUP_EXPLORER: {
            int cx = c->x, cy = c->y, player = c->owner;
            for (int step = 0; step < EXPLORER_NUM_STEPS; step++) {
                bool moved = false;
                for (int tries = 0; tries < EXPLORER_NUM_STEPS * 3 && !moved; tries++) {
                    int dir = poly_rand_int(&s->rng, 8);
                    int x = cx + PUSH_DX[dir], y = cy + PUSH_DY[dir];
                    if (in_bounds(s, x, y) && poly_traversable(s, x, y, player)) {
                        moved = true; cx = x; cy = y;
                        if (poly_clear_view(s, player, cx, cy, EXPLORER_CLEAR_RANGE))
                            poly_network_update_player(s, player, player == s->active_player);
                    }
                }
            }
            break;
        }
        case LEVELUP_CITY_WALL: c->has_walls = true; break;
        case LEVELUP_RESOURCES: p->stars += CITY_LEVEL_UP_RESOURCES; break;
        case LEVELUP_POP_GROWTH: city_add_population(s, ci, CITY_LEVEL_UP_POP_GROWTH); break;
        case LEVELUP_BORDER_GROWTH:
            c->bound = (int8_t)(c->bound + CITY_EXPANSION_TILES);
            poly_assign_city_tiles(s, ci, c->bound);
            break;
        case LEVELUP_PARK:
            p->score += CITY_LEVEL_UP_PARK;
            c->points_worth = (int16_t)(c->points_worth + CITY_LEVEL_UP_PARK);
            break;
        case LEVELUP_SUPERUNIT: {
            int t = tile_idx(s, c->x, c->y);
            int16_t occupant = s->unit_at[t];
            if (occupant >= 0 && !poly_push_unit(s, occupant)) poly_kill_unit(s, occupant);
            int ui = poly_new_unit(s, UNIT_SUPERUNIT, c->owner, c->x, c->y);
            city_add_unit(s, ci, ui);
            break;
        }
        default: break;
    }
    return true;
}

static inline bool exec_gather(PolyState* s, int ci, int tile) {
    if (!feas_gather(s, ci, tile)) return false;
    Resource r = (Resource)s->resource[tile];
    s->resource[tile] = RESOURCE_NONE;
    City* c = &s->cities[ci];
    s->players[c->owner].stars -= RESOURCE_INFO[r].cost;
    if (r == RESOURCE_WHALES) s->players[c->owner].stars += RESOURCE_INFO[r].star_bonus;
    else city_add_population(s, ci, RESOURCE_INFO[r].pop_bonus);
    return true;
}

static inline bool exec_forest_op(PolyState* s, int ci, int tile, ActKind kind) {
    if (!feas_forest_op(s, ci, tile, kind)) return false;
    Player* p = &s->players[s->cities[ci].owner];
    switch (kind) {
        case ACT_CLEAR_FOREST:      // note: resource is NOT cleared (Java quirk)
            s->terrain[tile] = TERRAIN_PLAIN;
            p->stars += CLEAR_FOREST_STAR;
            break;
        case ACT_BURN_FOREST:
            s->terrain[tile] = TERRAIN_PLAIN;
            s->resource[tile] = RESOURCE_CROPS;
            p->stars -= BURN_FOREST_COST;
            break;
        case ACT_GROW_FOREST:
            s->terrain[tile] = TERRAIN_FOREST;
            s->resource[tile] = RESOURCE_NONE;
            p->stars -= GROW_FOREST_COST;
            break;
        default: return false;
    }
    return true;
}

static inline bool exec_destroy(PolyState* s, int ci, int tile) {
    if (!feas_destroy(s, ci, tile)) return false;
    BuildingType b = (BuildingType)s->building[tile];
    s->building[tile] = BUILDING_NONE;
    if (b == BUILDING_PORT) poly_set_net_tile(s, tile, false);
    city_building_effects(s, ci, tile, b, true, false);
    s->temple_level[tile] = 0;
    s->temple_turns[tile] = 0;
    return true;
}

static inline bool exec_research(PolyState* s, int player, Tech t) {
    if (!feas_research(s, player, t)) return false;
    Player* p = &s->players[player];
    p->stars -= tech_cost(t, p->num_cities, tech_researched(p, TECH_PHILOSOPHY));
    p->techs |= 1u << t;
    p->score += TECH_INFO[t].tier * TECH_TIER_POINTS;
    if ((p->techs & ((1u << NUM_TECH) - 1)) == ((1u << NUM_TECH) - 1) &&
        p->monuments[BUILDING_TOWER_OF_WISDOM - 12] == MONUMENT_UNAVAILABLE)
        p->monuments[BUILDING_TOWER_OF_WISDOM - 12] = MONUMENT_AVAILABLE;
    return true;
}

static inline bool exec_build_road(PolyState* s, int player, int tile) {
    if (!feas_build_road(s, player, tile)) return false;
    s->players[player].stars -= ROAD_COST;
    poly_set_net_tile(s, tile, true);
    return true;
}

// ---------------------------------------------------------------------------
// Dispatcher + enumeration
// ---------------------------------------------------------------------------

// Applies one action for the ACTIVE player. END_TURN only flags the intent;
// the turn machine (game.h) finalizes it.
static inline bool poly_execute_action(PolyState* s, const PolyAction* a) {
    switch ((ActKind)a->kind) {
        case ACT_END_TURN:     return !player_has_levelup_pending(s, s->active_player);
        case ACT_RESEARCH:     return exec_research(s, s->active_player, (Tech)a->arg);
        case ACT_BUILD_ROAD:   return exec_build_road(s, s->active_player, a->target);
        case ACT_MOVE:         return exec_move(s, a->actor, a->target);
        case ACT_ATTACK:       return exec_attack(s, a->actor, a->target);
        case ACT_CAPTURE:      return exec_capture(s, a->actor);
        case ACT_RECOVER:      return exec_recover(s, a->actor);
        case ACT_HEAL_OTHERS:  return exec_heal_others(s, a->actor);
        case ACT_CONVERT:      return exec_convert(s, a->actor, a->target);
        case ACT_MAKE_VETERAN: return exec_make_veteran(s, a->actor);
        case ACT_UPGRADE:      return exec_upgrade(s, a->actor);
        case ACT_DISBAND:      return exec_disband(s, a->actor);
        case ACT_EXAMINE:      return exec_examine(s, a->actor);
        case ACT_BUILD:        return exec_build(s, a->actor, a->target, (BuildingType)a->arg);
        case ACT_SPAWN:        return exec_spawn(s, a->actor, (UnitType)a->arg);
        case ACT_LEVELUP:      return exec_levelup(s, a->actor, (LevelUpBonus)a->arg);
        case ACT_GATHER:       return exec_gather(s, a->actor, a->target);
        case ACT_CLEAR_FOREST:
        case ACT_BURN_FOREST:
        case ACT_GROW_FOREST:  return exec_forest_op(s, a->actor, a->target, (ActKind)a->kind);
        case ACT_DESTROY:      return exec_destroy(s, a->actor, a->target);
        default: return false;
    }
}

#define POLY_MAX_ACTIONS 2048

static inline void act_push(PolyAction* out, int* n, int cap,
                            ActKind k, int actor, int target, int arg) {
    if (*n < cap) out[(*n)++] = (PolyAction){(int8_t)k, (int16_t)actor, (int16_t)target, (int8_t)arg};
}

// All legal actions for the active player (GameState.computePlayerActions).
// Java's per-city rule: a city that can level up offers ONLY LevelUp actions.
static inline int poly_enumerate_actions(PolyState* s, PolyAction* out, int cap) {
    int n = 0;
    int player = s->active_player;
    Player* p = &s->players[player];
    int ntiles = s->size * s->size;

    // --- city actions ---
    // Java computePlayerActions: the FIRST city (in list order) that can level
    // up locks the whole action set to ONLY its LevelUp choices — no other
    // city, unit or tribe actions, and EndTurn is infeasible.
    for (int k = 0; k < p->num_cities; k++) {
        int ci = p->city_list[k];
        City* c = &s->cities[ci];
        if (c->population >= c->population_need) {
            n = 0;
            for (int b = 0; b < NUM_LEVELUP; b++)
                if (feas_levelup(s, ci, (LevelUpBonus)b))
                    act_push(out, &n, cap, ACT_LEVELUP, ci, -1, b);
            return n;
        }
        for (int t = 0; t < ntiles; t++) {
            if (s->city_at[t] != ci) continue;
            for (int b = 0; b < NUM_BUILDING; b++)
                if (feas_build(s, ci, t, (BuildingType)b))
                    act_push(out, &n, cap, ACT_BUILD, ci, t, b);
            if (feas_gather(s, ci, t)) act_push(out, &n, cap, ACT_GATHER, ci, t, -1);
            if (feas_forest_op(s, ci, t, ACT_CLEAR_FOREST)) act_push(out, &n, cap, ACT_CLEAR_FOREST, ci, t, -1);
            if (feas_forest_op(s, ci, t, ACT_BURN_FOREST)) act_push(out, &n, cap, ACT_BURN_FOREST, ci, t, -1);
            if (feas_forest_op(s, ci, t, ACT_GROW_FOREST)) act_push(out, &n, cap, ACT_GROW_FOREST, ci, t, -1);
            if (feas_destroy(s, ci, t)) act_push(out, &n, cap, ACT_DESTROY, ci, t, -1);
        }
        for (int ut = 0; ut < NUM_UNIT; ut++)
            if (feas_spawn(s, ci, (UnitType)ut))
                act_push(out, &n, cap, ACT_SPAWN, ci, -1, ut);
    }

    // --- unit actions ---
    for (int ui = 0; ui < s->num_units; ui++) {
        Unit* u = &s->units[ui];
        if (u->type == UNIT_NONE || u->owner != player) continue;
        if (feas_upgrade(s, ui)) act_push(out, &n, cap, ACT_UPGRADE, ui, -1, -1);  // Java: even when FINISHED
        if (u->status == STATUS_FINISHED) continue;
        if (unit_can_move(u)) {
            uint8_t dest_ok[MAX_TILES];
            poly_reachable(s, ui, dest_ok);
            for (int t = 0; t < ntiles; t++)
                if (dest_ok[t]) act_push(out, &n, cap, ACT_MOVE, ui, t, -1);
        }
        if (unit_can_attack(u) && u->type != UNIT_MIND_BENDER) {
            int r = UNIT_STATS[u->type].range;
            for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
                if (!dx && !dy) continue;
                int x = u->x + dx, y = u->y + dy;
                if (!in_bounds(s, x, y)) continue;
                int16_t ti = s->unit_at[tile_idx(s, x, y)];
                if (ti >= 0 && feas_attack(s, ui, ti))
                    act_push(out, &n, cap, ACT_ATTACK, ui, ti, -1);
            }
        }
        if (u->type == UNIT_MIND_BENDER && unit_can_attack(u)) {
            int r = UNIT_STATS[u->type].range;
            for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
                if (!dx && !dy) continue;
                int x = u->x + dx, y = u->y + dy;
                if (!in_bounds(s, x, y)) continue;
                int16_t ti = s->unit_at[tile_idx(s, x, y)];
                if (ti >= 0 && feas_convert(s, ui, ti))
                    act_push(out, &n, cap, ACT_CONVERT, ui, ti, -1);
            }
            if (feas_heal_others(s, ui)) act_push(out, &n, cap, ACT_HEAL_OTHERS, ui, -1, -1);
        }
        if (feas_capture(s, ui)) act_push(out, &n, cap, ACT_CAPTURE, ui, -1, -1);
        if (feas_recover(s, ui)) act_push(out, &n, cap, ACT_RECOVER, ui, -1, -1);
        if (feas_disband(s, ui)) act_push(out, &n, cap, ACT_DISBAND, ui, -1, -1);
        if (feas_examine(s, ui)) act_push(out, &n, cap, ACT_EXAMINE, ui, -1, -1);
        if (feas_make_veteran(s, ui)) act_push(out, &n, cap, ACT_MAKE_VETERAN, ui, -1, -1);
    }

    // --- tribe actions ---
    if (tech_researched(p, TECH_ROADS) && p->stars >= ROAD_COST) {
        for (int t = 0; t < ntiles; t++)
            if (feas_build_road(s, player, t))
                act_push(out, &n, cap, ACT_BUILD_ROAD, -1, t, -1);
    }
    for (int t = 0; t < NUM_TECH; t++)
        if (feas_research(s, player, (Tech)t))
            act_push(out, &n, cap, ACT_RESEARCH, -1, -1, t);
    if (!player_has_levelup_pending(s, player))
        act_push(out, &n, cap, ACT_END_TURN, -1, -1, -1);

    return n;
}

#endif // POLY_ACTIONS_H
