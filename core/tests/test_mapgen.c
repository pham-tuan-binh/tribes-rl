// Map generation tests: structural invariants across many seeds, plus full
// random games on generated maps (integration fuzz for the whole engine).
#include <stdio.h>
#include <string.h>
#include "../mapgen.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static PolyState S;

static void test_map_invariants(void) {
    const int8_t tribes[2] = {TRIBE_IMPERIUS, TRIBE_BARDUR};
    for (uint32_t seed = 1; seed <= 50; seed++) {
        poly_reset(&S, 2, tribes, 11, MODE_CAPITALS, seed, false);
        int ntiles = 11 * 11;

        // capitals: distinct CITY tiles in the interior, owned, with a unit
        for (int p = 0; p < 2; p++) {
            City* c = &S.cities[S.players[p].capital];
            CHECK(c->is_capital && c->owner == p);
            CHECK(c->x >= 2 && c->x < 9 && c->y >= 2 && c->y < 9);
            int t = tile_idx(&S, c->x, c->y);
            CHECK(S.terrain[t] == TERRAIN_CITY);
            CHECK(S.city_at[t] == S.players[p].capital);
            int16_t ui = S.unit_at[t];
            CHECK(ui >= 0 && S.units[ui].owner == p);
            CHECK(S.units[ui].type == TRIBE_INFO[tribes[p]].starting_unit);
        }
        CHECK(S.players[0].capital != S.players[1].capital);

        // tiles near capitals get post-generation fixups (Java postGenerate)
        // that may overwrite terrain under existing resources — skip those in
        // the consistency checks below, as Java allows the same artifacts.
        uint8_t fixup_zone[MAX_TILES];
        memset(fixup_zone, 0, sizeof(fixup_zone));
        for (int p = 0; p < 2; p++) {
            City* c = &S.cities[S.players[p].capital];
            for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                int x = c->x + dx, y = c->y + dy;
                if (in_bounds(&S, x, y)) fixup_zone[tile_idx(&S, x, y)] = 1;
            }
        }

        // terrain sanity: some land, some water on almost all seeds; villages exist
        int land = 0, village = 0, shallow_bad = 0, ruins = 0;
        for (int t = 0; t < ntiles; t++) {
            Terrain ter = (Terrain)S.terrain[t];
            CHECK(ter >= TERRAIN_PLAIN && ter <= TERRAIN_FOREST);
            if (!terrain_is_water(ter)) land++;
            if (ter == TERRAIN_VILLAGE) village++;
            if (S.resource[t] == RESOURCE_RUINS) {
                ruins++;
                CHECK(fixup_zone[t] || ter != TERRAIN_SHALLOW_WATER);
            }
            // deep water directly cross-adjacent to land should not exist
            if (ter == TERRAIN_DEEP_WATER) {
                int x = t % 11, y = t / 11;
                const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (!in_bounds(&S, nx, ny)) continue;
                    Terrain tn = (Terrain)S.terrain[tile_idx(&S, nx, ny)];
                    if (!terrain_is_water(tn)) shallow_bad++;
                }
            }
        }
        CHECK(land > ntiles / 4);
        CHECK(village >= 1);
        CHECK(shallow_bad == 0);
        CHECK(ruins <= 3 + 1);            // round(121/40)=3 target

        // resources only on sensible terrain (outside the fixup zone)
        for (int t = 0; t < ntiles; t++) {
            if (fixup_zone[t]) continue;
            switch (S.resource[t]) {
                case RESOURCE_FISH: CHECK(S.terrain[t] == TERRAIN_SHALLOW_WATER); break;
                case RESOURCE_WHALES: CHECK(S.terrain[t] == TERRAIN_DEEP_WATER); break;
                case RESOURCE_ORE: CHECK(S.terrain[t] == TERRAIN_MOUNTAIN); break;
                case RESOURCE_ANIMAL: CHECK(S.terrain[t] == TERRAIN_FOREST); break;
                case RESOURCE_CROPS:
                case RESOURCE_FRUIT: CHECK(S.terrain[t] == TERRAIN_PLAIN); break;
                default: break;
            }
        }

        // Imperius fruit fixup / Bardur animal fixup around capitals
        int fruit = 0, animal = 0;
        int16_t ring[16];
        int rn = mg_circle(tile_idx(&S, S.cities[S.players[0].capital].x,
                                    S.cities[S.players[0].capital].y), 1, 11, ring);
        for (int k = 0; k < rn; k++) if (S.resource[ring[k]] == RESOURCE_FRUIT) fruit++;
        rn = mg_circle(tile_idx(&S, S.cities[S.players[1].capital].x,
                                S.cities[S.players[1].capital].y), 1, 11, ring);
        for (int k = 0; k < rn; k++) if (S.resource[ring[k]] == RESOURCE_ANIMAL) animal++;
        CHECK(fruit >= 2);
        CHECK(animal >= 2);

        // game is live: player 0 to act with legal actions available
        CHECK(S.active_player == 0 && !S.game_over);
        CHECK(S.players[0].stars == INITIAL_STARS);
        if (failures) { fprintf(stderr, "  (seed %u)\n", seed); return; }
    }
}

static void test_scores_at_start(void) {
    // Imperius: tech 100 + centre 100 + warrior 10 + 9 border tiles * 20 = 390
    const int8_t tribes[2] = {TRIBE_IMPERIUS, TRIBE_LUXIDOOR};
    poly_reset(&S, 2, tribes, 11, MODE_CAPITALS, 42, false);
    CHECK(S.players[0].score == 100 + 100 + 10 + 9 * 20);
    // Luxidoor: no tech, L3 walled capital: 0 + 100 + 10 + 180 = 290
    CHECK(S.players[1].score == 100 + 10 + 9 * 20);
    CHECK(S.cities[S.players[1].capital].level == 3);
    CHECK(S.cities[S.players[1].capital].has_walls);
}

static void test_full_games_on_generated_maps(void) {
    const int8_t tribes[2] = {TRIBE_XIN_XI, TRIBE_OUMAJI};
    for (uint32_t seed = 100; seed < 130; seed++) {
        poly_reset(&S, 2, tribes, 11, MODE_CAPITALS, seed, false);
        S.max_turns = 50;   // bounded games for tests (product rules have no cap)
        PolyAction acts[POLY_MAX_ACTIONS];
        long steps = 0;
        while (!S.game_over && steps < 200000) {
            int n = poly_enumerate_actions(&S, acts, POLY_MAX_ACTIONS);
            CHECK(n >= 1);
            PolyAction* a = &acts[poly_rand_int(&S.rng, n)];
            if (poly_rand_int(&S.rng, 8) == 0) {
                for (int i = 0; i < n; i++)
                    if (acts[i].kind == ACT_END_TURN) { a = &acts[i]; break; }
            }
            CHECK(poly_step(&S, a));
            steps++;
            if (failures) { fprintf(stderr, "  (seed %u, step %ld)\n", seed, steps); return; }
        }
        CHECK(S.game_over);
        int winners = 0;
        for (int p = 0; p < 2; p++) winners += S.players[p].result == RESULT_WIN;
        CHECK(winners == 1);
        if (failures) { fprintf(stderr, "  (seed %u)\n", seed); return; }
    }
}

int main(void) {
    test_map_invariants();
    test_scores_at_start();
    test_full_games_on_generated_maps();
    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all mapgen tests passed\n");
    return 0;
}
