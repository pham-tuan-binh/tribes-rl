// Turn machine tests + full random self-play games on a handcrafted board.
#include <stdio.h>
#include <string.h>
#include "../game.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static PolyState S;

static void board_reset(int size, GameMode mode) {
    memset(&S, 0, sizeof(S));
    S.size = (int8_t)size;
    S.num_players = 2;
    S.mode = mode;
    S.rng = 7;
    for (int i = 0; i < MAX_TILES; i++) {
        S.terrain[i] = TERRAIN_PLAIN; S.resource[i] = RESOURCE_NONE;
        S.building[i] = BUILDING_NONE; S.unit_at[i] = -1; S.city_at[i] = -1;
    }
    for (int p = 0; p < 2; p++) {
        memset(S.players[p].obs, 0xff, sizeof(S.players[p].obs));
        S.players[p].result = RESULT_INCOMPLETE;
        S.players[p].capital = -1;
    }
}

static int mk_city(int owner, int x, int y, bool capital) {
    int ci = S.num_cities++;
    City* c = &S.cities[ci];
    memset(c, 0, sizeof(*c));
    c->x = (int8_t)x; c->y = (int8_t)y; c->owner = (int8_t)owner;
    c->level = 1; c->population_need = 2; c->bound = 1; c->is_capital = capital;
    S.terrain[tile_idx(&S, x, y)] = TERRAIN_CITY;
    S.net_tile[tile_idx(&S, x, y)] = 1;
    Player* p = &S.players[owner];
    p->city_list[p->num_cities++] = (int8_t)ci;
    if (capital) p->capital = (int8_t)ci;
    poly_assign_city_tiles(&S, ci, 1);
    return ci;
}

static int mk_unit(UnitType type, int owner, int x, int y, int city, TurnStatus st) {
    int ui = poly_new_unit(&S, type, owner, x, y);
    if (city >= 0) city_add_unit(&S, city, ui);
    S.units[ui].status = (int8_t)st;
    return ui;
}

static void test_income_and_rotation(void) {
    board_reset(9, MODE_CAPITALS);
    int c0 = mk_city(0, 2, 2, true);
    mk_city(1, 6, 6, true);
    poly_begin(&S);
    CHECK(S.active_player == 0 && S.tick == 0);
    CHECK(S.players[0].stars == INITIAL_STARS);      // tick-0 special
    PolyAction end = {ACT_END_TURN, -1, -1, -1};
    CHECK(poly_step(&S, &end));
    CHECK(S.active_player == 1 && S.tick == 0);      // same round
    CHECK(S.players[1].stars == INITIAL_STARS);
    CHECK(poly_step(&S, &end));
    CHECK(S.active_player == 0 && S.tick == 1);      // new round
    // level-1 capital production: 1 (level) + 1 (capital) = 2
    CHECK(S.players[0].stars == INITIAL_STARS + 2);
    // enemy unit parked on the city center blocks its production
    mk_unit(UNIT_WARRIOR, 1, 2, 2, -1, STATUS_FINISHED);
    int stars0 = S.players[0].stars;
    CHECK(poly_step(&S, &end));                      // to player 1
    CHECK(poly_step(&S, &end));                      // back to player 0
    CHECK(S.players[0].stars == stars0);             // no income from c0
    (void)c0;
}

static void test_auto_recover_and_refresh(void) {
    board_reset(9, MODE_CAPITALS);
    int c0 = mk_city(0, 2, 2, true);
    mk_city(1, 6, 6, true);
    int u = mk_unit(UNIT_WARRIOR, 0, 3, 2, c0, STATUS_FRESH);   // inside own borders
    S.units[u].hp = 5;
    poly_begin(&S);
    PolyAction end = {ACT_END_TURN, -1, -1, -1};
    CHECK(poly_step(&S, &end));
    CHECK(S.units[u].hp == 9);                       // +2 recover +2 in-borders
    CHECK(S.units[u].status == STATUS_FINISHED);
    CHECK(poly_step(&S, &end));                      // round back to player 0
    CHECK(S.units[u].status == STATUS_FRESH);        // refreshed at initTurn
}

static void test_temple_growth(void) {
    board_reset(9, MODE_CAPITALS);
    int c0 = mk_city(0, 2, 2, true);
    (void)c0;
    mk_city(1, 6, 6, true);
    int t = tile_idx(&S, 3, 2);
    S.building[t] = BUILDING_TEMPLE;
    S.temple_level[t] = 1; S.temple_turns[t] = TEMPLE_TURNS_TO_SCORE;
    poly_begin(&S);
    int score0 = S.players[0].score;
    PolyAction end = {ACT_END_TURN, -1, -1, -1};
    // 3 full rounds -> temple levels to 2, +50 points
    for (int i = 0; i < 6; i++) CHECK(poly_step(&S, &end));
    CHECK(S.temple_level[t] == 2);
    CHECK(S.players[0].score == score0 + TEMPLE_POINTS[1]);
}

static void test_capitals_win(void) {
    board_reset(9, MODE_CAPITALS);
    int c0 = mk_city(0, 2, 2, true);
    mk_city(1, 6, 6, true);
    int u = mk_unit(UNIT_WARRIOR, 0, 6, 6, c0, STATUS_FRESH);   // standing on enemy capital
    poly_begin(&S);
    PolyAction cap = {ACT_CAPTURE, (int16_t)u, -1, -1};
    CHECK(poly_step(&S, &cap));
    CHECK(S.game_over);
    CHECK(S.players[0].result == RESULT_WIN && S.players[1].result == RESULT_LOSS);
}

static void test_full_random_games(void) {
    // Full random self-play games must terminate within the turn cap without
    // invariant violations. Symmetric hand-built map with villages/resources.
    for (int seed = 1; seed <= 20; seed++) {
        board_reset(11, MODE_CAPITALS);
        S.rng = (uint32_t)seed;
        int c0 = mk_city(0, 2, 2, true);
        int c1 = mk_city(1, 8, 8, true);
        S.terrain[tile_idx(&S, 5, 5)] = TERRAIN_VILLAGE;
        S.terrain[tile_idx(&S, 2, 8)] = TERRAIN_VILLAGE;
        S.terrain[tile_idx(&S, 8, 2)] = TERRAIN_VILLAGE;
        S.terrain[tile_idx(&S, 4, 2)] = TERRAIN_FOREST;
        S.resource[tile_idx(&S, 4, 2)] = RESOURCE_ANIMAL;
        S.terrain[tile_idx(&S, 8, 6)] = TERRAIN_FOREST;
        S.resource[tile_idx(&S, 3, 2)] = RESOURCE_FRUIT;
        S.resource[tile_idx(&S, 8, 7)] = RESOURCE_FRUIT;
        S.resource[tile_idx(&S, 6, 5)] = RESOURCE_RUINS;
        S.terrain[tile_idx(&S, 5, 8)] = TERRAIN_MOUNTAIN;
        S.resource[tile_idx(&S, 5, 8)] = RESOURCE_ORE;
        S.players[0].techs |= 1u << TRIBE_INFO[TRIBE_IMPERIUS].initial_tech;
        S.players[1].techs |= 1u << TRIBE_INFO[TRIBE_BARDUR].initial_tech;
        mk_unit(UNIT_WARRIOR, 0, 2, 2, c0, STATUS_FRESH);
        mk_unit(UNIT_WARRIOR, 1, 8, 8, c1, STATUS_FRESH);
        poly_begin(&S);

        PolyAction acts[POLY_MAX_ACTIONS];
        long steps = 0;
        while (!S.game_over && steps < 100000) {
            int n = poly_enumerate_actions(&S, acts, POLY_MAX_ACTIONS);
            CHECK(n >= 1);
            PolyAction* a = &acts[poly_rand_int(&S.rng, n)];
            // bias: 1/8 chance to just end the turn when legal, keeps games moving
            if (poly_rand_int(&S.rng, 8) == 0) {
                for (int i = 0; i < n; i++)
                    if (acts[i].kind == ACT_END_TURN) { a = &acts[i]; break; }
            }
            CHECK(poly_step(&S, a));
            steps++;
            // invariants: unit_at consistency
            if (steps % 500 == 0) {
                for (int t = 0; t < S.size * S.size; t++) {
                    int16_t ui = S.unit_at[t];
                    if (ui >= 0) {
                        CHECK(S.units[ui].type != UNIT_NONE);
                        CHECK(tile_idx(&S, S.units[ui].x, S.units[ui].y) == t);
                    }
                }
            }
            if (failures) { fprintf(stderr, "  (seed %d, step %ld)\n", seed, steps); return; }
        }
        CHECK(S.game_over);
        CHECK(S.tick <= MAX_TURNS_CAPITALS + 1);
        // exactly one winner
        int winners = 0;
        for (int p = 0; p < 2; p++) winners += S.players[p].result == RESULT_WIN;
        CHECK(winners == 1);
    }
}

int main(void) {
    test_income_and_rotation();
    test_auto_recover_and_refresh();
    test_temple_growth();
    test_capitals_win();
    test_full_random_games();
    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all game/turn-machine tests passed\n");
    return 0;
}
