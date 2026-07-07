// polytopia-rl — procedural map generation + game start.
// Port of src/core/levelgen/LevelGenerator.java (itself a port of
// QuasiStellar/Polytopia-Map-Generator) + LevelLoader.java game start.
//
// Deliberate divergences from the Java (documented; maps only need to be
// statistically equivalent — golden traces load Java-exported CSV maps):
//  - shallow-water pass converts the DEEP tile to shallow (as the original
//    generator and py/levelgen do); Java has a bug converting the land
//    neighbour instead (LevelGenerator.java:269).
//  - ruin dispersion marks around the RUIN tile; Java passes the villageMap
//    VALUE as the circle center (LevelGenerator.java:367), a no-op bug.
//  - RNG is our LCG, not java.util.Random; tie-break iteration is ascending.
#ifndef POLY_MAPGEN_H
#define POLY_MAPGEN_H

#include "game.h"

// terrainProbs.json — BASE row + per-tribe multiplier [category][tribe].
// (WATER category exists in the JSON but is unused by the Java generator.)
typedef enum { MG_FOREST, MG_MOUNTAIN, MG_ORE, MG_FRUIT, MG_CROPS, MG_ANIMAL,
               MG_FISH, MG_WHALES, NUM_MG } MgCategory;

static const double MG_BASE[NUM_MG] = {0.4, 0.15, 0.5, 0.5, 0.5, 0.5, 0.5, 0.4};

static const double MG_TRIBE[NUM_MG][NUM_TRIBE] = {
    //            XIN  IMP  BAR  OUM  KIC  HOO  LUX  VEN  ZEB  AIM  QUE  YAD
    [MG_FOREST]   = {1, 1, 1, 0.1, 1, 1.5, 1, 1, 0.5, 1, 1, 0.5},
    [MG_MOUNTAIN] = {1.5, 1, 1, 1, 0.5, 0.5, 1, 1, 0.5, 1.5, 1, 0.5},
    [MG_ORE]      = {1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 0.1, 1},
    [MG_FRUIT]    = {1, 1, 1.5, 1, 1, 1, 2, 0.1, 0.5, 1, 2, 1.5},
    [MG_CROPS]    = {1, 1, 0.1, 1, 1, 1, 1, 1, 1, 0.1, 0.1, 1},
    [MG_ANIMAL]   = {1, 1, 2, 1, 1, 1, 0.5, 0.1, 1, 1, 1, 1},
    [MG_FISH]     = {1, 1, 1, 1, 1.5, 1, 1, 0.1, 1, 1, 1, 1},
    [MG_WHALES]   = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

#define MG_BORDER_EXPANSION (1.0 / 3.0)

static inline double poly_rand_double(uint32_t* rng) {
    return (double)poly_rand(rng) / 2147483648.0;   // [0,1)
}
static inline int mg_rand_int(uint32_t* rng, int min, int max) {  // [min,max)
    return (int)((double)min + poly_rand_double(rng) * (max - min));
}

static inline double mg_prob(MgCategory cat, int tribe /* TribeType or -1 */) {
    return MG_BASE[cat] * (tribe < 0 ? 1.0 : MG_TRIBE[cat][tribe]);
}

// LevelGenerator.circle: ring at Chebyshev radius (verbatim clipping quirks).
static inline int mg_circle(int center, int radius, int size, int16_t* out) {
    int n = 0;
    int row = center / size, column = center % size;
    int i = row - radius;
    if (i >= 0 && i < size)
        for (int j = column - radius; j < column + radius; j++)
            if (j >= 0 && j < size) out[n++] = (int16_t)(i * size + j);
    i = row + radius;
    if (i >= 0 && i < size)
        for (int j = column + radius; j > column - radius; j--)
            if (j >= 0 && j < size) out[n++] = (int16_t)(i * size + j);
    int j = column - radius;
    if (j >= 0 && j < size)
        for (i = row + radius; i > row - radius; i--)
            if (i >= 0 && i < size) out[n++] = (int16_t)(i * size + j);
    j = column + radius;
    if (j >= 0 && j < size)
        for (i = row - radius; i < row + radius; i++)
            if (i >= 0 && i < size) out[n++] = (int16_t)(i * size + j);
    return n;
}

static inline int mg_cross(int center, int size, int16_t* out) {
    int n = 0, row = center / size, column = center % size;
    if (column > 0) out[n++] = (int16_t)(center - 1);
    if (column < size - 1) out[n++] = (int16_t)(center + 1);
    if (row > 0) out[n++] = (int16_t)(center - size);
    if (row < size - 1) out[n++] = (int16_t)(center + size);
    return n;
}

// proc(): tier-2 tiles at full probability, tier-1 at prob/3.
static inline bool mg_proc(uint32_t* rng, const int8_t* vm, int cell, double prob) {
    if (vm[cell] == 2) return poly_rand_double(rng) < prob;
    if (vm[cell] == 1) return poly_rand_double(rng) < prob * MG_BORDER_EXPANSION;
    return false;
}

// Generates terrain/resource/capitals. tribes[i] = TribeType of player i.
typedef struct {
    int8_t terrain[MAX_TILES];
    int8_t resource[MAX_TILES];
    int16_t capital_tile[MAX_PLAYERS];
} MgResult;

static inline void poly_generate_map(MgResult* out, int size, int num_players,
                                     const int8_t* tribes, uint32_t* rng) {
    int ntiles = size * size;
    int8_t* terr = out->terrain;
    int8_t* res = out->resource;
    int8_t owner[MAX_TILES];
    int8_t vm[MAX_TILES];
    int16_t ring[16];

    for (int t = 0; t < ntiles; t++) { terr[t] = TERRAIN_DEEP_WATER; res[t] = RESOURCE_NONE; owner[t] = -1; }

    // 1. random initial land
    double initial_land = 0.5;
    int placed = 0;
    while (placed < (int)(ntiles * initial_land)) {
        int idx = mg_rand_int(rng, 0, ntiles);
        if (terr[idx] == TERRAIN_DEEP_WATER) { placed++; terr[idx] = TERRAIN_PLAIN; }
    }

    // 2. smoothing (x3, relief 4): ground iff water fraction in disk <= (0.5+4)/9
    const int smoothing = 3;
    const double land_coefficient = (0.5 + 4) / 9.0;
    for (int pass = 0; pass < smoothing; pass++) {
        uint8_t to_ground[MAX_TILES];
        for (int cell = 0; cell < ntiles; cell++) {
            int water = 0, count = 1;   // disk includes center
            if (terr[cell] == TERRAIN_DEEP_WATER) water++;
            int rn = mg_circle(cell, 1, size, ring);
            for (int k = 0; k < rn; k++) { count++; if (terr[ring[k]] == TERRAIN_DEEP_WATER) water++; }
            to_ground[cell] = (double)water / count <= land_coefficient;
        }
        for (int cell = 0; cell < ntiles; cell++)
            terr[cell] = to_ground[cell] ? TERRAIN_PLAIN : TERRAIN_DEEP_WATER;
    }

    // 3. capital placement: interior PLAIN tiles, maximize min-distance, random ties
    int16_t caps[MAX_PLAYERS];
    for (int p = 0; p < num_players; p++) {
        int best = -1;
        int16_t cand[MAX_TILES]; int n_cand = 0;
        for (int row = 2; row < size - 2; row++) for (int col = 2; col < size - 2; col++) {
            int cell = row * size + col;
            if (terr[cell] != TERRAIN_PLAIN) continue;
            int v = size;
            for (int q = 0; q < p; q++) {
                int d = chebyshev(cell % size, cell / size, caps[q] % size, caps[q] / size);
                if (d < v) v = d;
            }
            if (v > best) { best = v; n_cand = 0; }
            if (v == best) cand[n_cand++] = (int16_t)cell;
        }
        caps[p] = n_cand > 0 ? cand[mg_rand_int(rng, 0, n_cand)]
                             : (int16_t)(size / 2 * size + size / 2);  // degenerate map fallback
        terr[caps[p]] = TERRAIN_CITY;
        out->capital_tile[p] = caps[p];
    }

    // 4. territory expansion: round-robin flood fill from capitals
    uint8_t done[MAX_TILES];
    memset(done, 0, (size_t)ntiles);
    int done_count = 0;
    static int16_t active[MAX_PLAYERS][MAX_TILES];
    int n_active[MAX_PLAYERS];
    for (int p = 0; p < num_players; p++) {
        done[caps[p]] = 1; done_count++;
        active[p][0] = caps[p]; n_active[p] = 1;
        owner[caps[p]] = tribes[p];
    }
    while (done_count < ntiles) {
        bool any = false;
        for (int p = 0; p < num_players; p++) {
            if (n_active[p] == 0) continue;
            any = true;
            int slot = mg_rand_int(rng, 0, n_active[p]);
            int cell = active[p][slot];
            int rn = mg_circle(cell, 1, size, ring);
            int16_t valid[16]; int n_valid = 0;
            for (int k = 0; k < rn; k++)
                if (!done[ring[k]] && terr[ring[k]] != TERRAIN_DEEP_WATER) valid[n_valid++] = ring[k];
            if (n_valid == 0)
                for (int k = 0; k < rn; k++)
                    if (!done[ring[k]]) valid[n_valid++] = ring[k];
            if (n_valid > 0) {
                int claimed = valid[mg_rand_int(rng, 0, n_valid)];
                owner[claimed] = tribes[p];
                active[p][n_active[p]++] = (int16_t)claimed;
                done[claimed] = 1; done_count++;
            } else {
                active[p][slot] = active[p][--n_active[p]];   // deactivate
            }
        }
        if (!any) break;   // safety: unreachable tiles keep owner -1
    }

    // 5. forest & mountain on plains
    for (int cell = 0; cell < ntiles; cell++) {
        if (terr[cell] != TERRAIN_PLAIN) continue;
        double r = poly_rand_double(rng);
        if (r < mg_prob(MG_FOREST, owner[cell])) terr[cell] = TERRAIN_FOREST;
        else if (r > 1.0 - mg_prob(MG_MOUNTAIN, owner[cell])) terr[cell] = TERRAIN_MOUNTAIN;
    }

    // 6. villageMap tiers: -1 water/mountain/border, 0 far, 1 expansion, 2 initial, 3 village
    for (int cell = 0; cell < ntiles; cell++) {
        int row = cell / size, col = cell % size;
        if (terr[cell] == TERRAIN_DEEP_WATER || terr[cell] == TERRAIN_MOUNTAIN) vm[cell] = -1;
        else if (row == 0 || row == size - 1 || col == 0 || col == size - 1) vm[cell] = -1;
        else vm[cell] = 0;
    }

    // 7. shallow water: deep tiles cross-adjacent to land become shallow
    //    (original-generator semantics; Java bug converts the land tile instead)
    for (int cell = 0; cell < ntiles; cell++) {
        if (terr[cell] != TERRAIN_DEEP_WATER) continue;
        int cn = mg_cross(cell, size, ring);
        for (int k = 0; k < cn; k++) {
            int8_t tn = terr[ring[k]];
            if (tn == TERRAIN_PLAIN || tn == TERRAIN_FOREST || tn == TERRAIN_MOUNTAIN ||
                tn == TERRAIN_CITY) {
                terr[cell] = TERRAIN_SHALLOW_WATER;
                break;
            }
        }
    }

    // 8. mark capital surroundings, then villages until no far-away tiles remain
    for (int p = 0; p < num_players; p++) {
        vm[caps[p]] = 3;
        int rn = mg_circle(caps[p], 1, size, ring);
        for (int k = 0; k < rn; k++) if (vm[ring[k]] < 2) vm[ring[k]] = 2;
        rn = mg_circle(caps[p], 2, size, ring);
        for (int k = 0; k < rn; k++) if (vm[ring[k]] < 1) vm[ring[k]] = 1;
    }
    for (;;) {
        int v = -1;
        for (int cell = 0; cell < ntiles; cell++) if (vm[cell] == 0) { v = cell; break; }
        if (v < 0) break;
        vm[v] = 3;
        int rn = mg_circle(v, 1, size, ring);
        for (int k = 0; k < rn; k++) if (vm[ring[k]] < 2) vm[ring[k]] = 2;
        rn = mg_circle(v, 2, size, ring);
        for (int k = 0; k < rn; k++) if (vm[ring[k]] < 1) vm[ring[k]] = 1;
    }

    // 9. resources & village terrain
    for (int cell = 0; cell < ntiles; cell++) {
        switch (terr[cell]) {
            case TERRAIN_PLAIN: {
                double fruit = mg_prob(MG_FRUIT, owner[cell]);
                double crop = mg_prob(MG_CROPS, owner[cell]);
                if (vm[cell] == 3) terr[cell] = TERRAIN_VILLAGE;
                else if (mg_proc(rng, vm, cell, fruit * (1 - crop / 2))) res[cell] = RESOURCE_FRUIT;
                else if (mg_proc(rng, vm, cell, crop * (1 - fruit / 2))) res[cell] = RESOURCE_CROPS;
                break;
            }
            case TERRAIN_FOREST:
                if (vm[cell] == 3) { terr[cell] = TERRAIN_VILLAGE; res[cell] = RESOURCE_NONE; }
                else if (mg_proc(rng, vm, cell, mg_prob(MG_ANIMAL, owner[cell]))) res[cell] = RESOURCE_ANIMAL;
                break;
            case TERRAIN_SHALLOW_WATER:
                if (mg_proc(rng, vm, cell, mg_prob(MG_FISH, owner[cell]))) res[cell] = RESOURCE_FISH;
                break;
            case TERRAIN_DEEP_WATER:
                if (mg_proc(rng, vm, cell, mg_prob(MG_WHALES, owner[cell]))) res[cell] = RESOURCE_WHALES;
                break;
            case TERRAIN_MOUNTAIN:
                if (mg_proc(rng, vm, cell, mg_prob(MG_ORE, owner[cell]))) res[cell] = RESOURCE_ORE;
                break;
            default: break;
        }
    }

    // 10. ruins on far/expansion/water tiles; ~ntiles/40, one third on deep water,
    //     never on shallow water; dispersion marks around the placed ruin
    int ruins_target = (int)(ntiles / 40.0 + 0.5);
    int water_target = (int)(ruins_target / 3.0 + 0.5);
    int ruins = 0, water_ruins = 0;
    for (int guard = 0; ruins < ruins_target && guard < 10000; guard++) {
        int16_t cand[MAX_TILES]; int n_cand = 0;
        for (int cell = 0; cell < ntiles; cell++)
            if (vm[cell] == 0 || vm[cell] == 1 || vm[cell] == -1) cand[n_cand++] = (int16_t)cell;
        if (n_cand == 0) break;
        int ruin = cand[mg_rand_int(rng, 0, n_cand)];
        if (terr[ruin] == TERRAIN_SHALLOW_WATER) continue;
        if (terr[ruin] == TERRAIN_DEEP_WATER && water_ruins >= water_target) continue;
        res[ruin] = RESOURCE_RUINS;
        if (terr[ruin] == TERRAIN_DEEP_WATER) water_ruins++;
        int rn = mg_circle(ruin, 1, size, ring);
        for (int k = 0; k < rn; k++) if (vm[ring[k]] < 2) vm[ring[k]] = 2;
        if (vm[ruin] < 2) vm[ruin] = 2;   // don't re-pick the same tile
        ruins++;
    }

    // 11. capital fixups: Imperius 2 fruit on plain, Bardur 2 animal on forest
    for (int p = 0; p < num_players; p++) {
        int8_t want_res; int8_t want_terr;
        if (tribes[p] == TRIBE_IMPERIUS) { want_res = RESOURCE_FRUIT; want_terr = TERRAIN_PLAIN; }
        else if (tribes[p] == TRIBE_BARDUR) { want_res = RESOURCE_ANIMAL; want_terr = TERRAIN_FOREST; }
        else continue;
        int rn = mg_circle(caps[p], 1, size, ring);
        for (int guard = 0; guard < 100; guard++) {
            int count = 0;
            for (int k = 0; k < rn; k++) if (res[ring[k]] == want_res) count++;
            if (count >= 2) break;
            int pos = ring[mg_rand_int(rng, 0, rn)];
            terr[pos] = want_terr; res[pos] = want_res;
            int cn = mg_cross(pos, size, ring + 8);   // reuse tail of the buffer
            for (int k = 0; k < cn; k++)
                if (terr[ring[8 + k]] == TERRAIN_DEEP_WATER) terr[ring[8 + k]] = TERRAIN_SHALLOW_WATER;
        }
    }
}

// ---------------------------------------------------------------------------
// Game start (LevelLoader + Tribe.init): build a PolyState from a fresh map.
// ---------------------------------------------------------------------------
static inline void poly_reset(PolyState* s, int num_players, const int8_t* tribes,
                              int size, GameMode mode, uint32_t seed) {
    memset(s, 0, sizeof(*s));
    s->size = (int8_t)size;
    s->num_players = (int8_t)num_players;
    s->mode = mode;
    s->rng = seed ? seed : 1;

    MgResult map;
    poly_generate_map(&map, size, num_players, tribes, &s->rng);

    int ntiles = size * size;
    for (int t = 0; t < ntiles; t++) {
        s->terrain[t] = map.terrain[t];
        s->resource[t] = map.resource[t];
        s->building[t] = BUILDING_NONE;
        s->unit_at[t] = -1;
        s->city_at[t] = -1;
    }

    for (int p = 0; p < num_players; p++) {
        Player* pl = &s->players[p];
        pl->tribe = tribes[p];
        pl->capital = -1;
        pl->result = RESULT_INCOMPLETE;
        pl->stars = INITIAL_STARS;
        memset(pl->obs, 0xff, sizeof(pl->obs));   // v1: full observability

        // initial tech + its score (Tribe.init)
        int8_t tech = TRIBE_INFO[tribes[p]].initial_tech;
        if (tech != TECH_NONE) {
            pl->techs |= 1u << tech;
            pl->score = TECH_INFO[tech].tier * TECH_TIER_POINTS;
        }

        // capital city (LevelLoader)
        int cap_tile = map.capital_tile[p];
        int ci = s->num_cities++;
        City* c = &s->cities[ci];
        memset(c, 0, sizeof(*c));
        c->x = (int8_t)(cap_tile % size); c->y = (int8_t)(cap_tile / size);
        c->owner = (int8_t)p; c->is_capital = true;
        c->level = 1; c->population_need = 2; c->bound = 1;
        if (tribes[p] == TRIBE_LUXIDOOR) {   // no initial tech; L3 walled capital
            c->level = 3; c->population = 0; c->population_need = 4;
            c->has_walls = true;
        }
        pl->capital = (int8_t)ci;
        pl->city_list[pl->num_cities++] = (int8_t)ci;
        s->net_tile[cap_tile] = 1;
        pl->score += CITY_CENTRE_POINTS;

        // starting unit
        int ut = TRIBE_INFO[tribes[p]].starting_unit;
        int ui = poly_new_unit(s, (UnitType)ut, p, c->x, c->y);
        city_add_unit(s, ci, ui);
        pl->score += UNIT_STATS[ut].points;

        poly_assign_city_tiles(s, ci, c->bound);
    }

    poly_begin(s);
}

#endif // POLY_MAPGEN_H
