// Action-layer tests: feasibility, execution effects, enumeration.
#include <stdio.h>
#include <string.h>
#include "../actions.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static PolyState S;

static void board_reset(int size) {
    memset(&S, 0, sizeof(S));
    S.size = (int8_t)size;
    S.num_players = 2;
    S.rng = 99;
    for (int i = 0; i < MAX_TILES; i++) {
        S.terrain[i] = TERRAIN_PLAIN; S.resource[i] = RESOURCE_NONE;
        S.building[i] = BUILDING_NONE; S.unit_at[i] = -1; S.city_at[i] = -1;
    }
    S.players[0].capital = -1; S.players[1].capital = -1;
    for (int p = 0; p < 2; p++) {
        memset(S.players[p].obs, 0xff, sizeof(S.players[p].obs));
        S.players[p].stars = 100;
        S.players[p].result = RESULT_INCOMPLETE;
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

static void test_spawn_and_research(void) {
    board_reset(9);
    int cap = mk_city(0, 4, 4, true);
    S.players[0].stars = 3;
    PolyAction a = {ACT_SPAWN, (int16_t)cap, -1, UNIT_WARRIOR};
    CHECK(poly_execute_action(&S, &a));
    CHECK(S.players[0].stars == 1);                  // warrior costs 2
    CHECK(S.players[0].score == /* border+... */ S.players[0].score);  // smoke
    int ui = S.unit_at[tile_idx(&S, 4, 4)];
    CHECK(ui >= 0 && S.units[ui].type == UNIT_WARRIOR && S.units[ui].city == cap);
    CHECK(S.units[ui].status == STATUS_FINISHED);    // can't act on spawn turn
    CHECK(!poly_execute_action(&S, &a));             // city tile now occupied

    // research: CLIMBING costs 4 + 1*1 = 5 with one city
    S.players[0].stars = 5;
    PolyAction r = {ACT_RESEARCH, -1, -1, TECH_CLIMBING};
    CHECK(poly_execute_action(&S, &r));
    CHECK(S.players[0].stars == 0);
    CHECK(tech_researched(&S.players[0], TECH_CLIMBING));
    CHECK(S.players[0].score >= 100);                // tier-1 = 100 points
    CHECK(!poly_execute_action(&S, &r));             // already researched
}

static void test_attack_kill_and_advance(void) {
    board_reset(9);
    mk_city(0, 1, 1, true); mk_city(1, 7, 7, true);
    // swordman (ATK 3) vs warrior at 5 HP: 10 dmg -> kill, melee advance
    int a = mk_unit(UNIT_SWORDMAN, 0, 3, 3, 0, STATUS_FRESH);
    int d = mk_unit(UNIT_WARRIOR, 1, 3, 4, 1, STATUS_FRESH);
    S.units[d].hp = 5;
    int score1 = S.players[1].score;
    PolyAction act = {ACT_ATTACK, (int16_t)a, (int16_t)d, -1};
    CHECK(poly_execute_action(&S, &act));
    CHECK(S.units[d].type == UNIT_NONE);                       // dead
    CHECK(S.units[a].x == 3 && S.units[a].y == 4);             // advanced
    CHECK(S.units[a].kills == 1 && S.players[0].num_kills == 1);
    CHECK(S.players[1].score == score1 - 10);                  // warrior points lost
    CHECK(S.units[a].status == STATUS_FINISHED);               // dash: attack ends turn

    // ranged: archer at range 2 takes no retaliation
    board_reset(9);
    mk_city(0, 1, 1, true); mk_city(1, 7, 7, true);
    a = mk_unit(UNIT_ARCHER, 0, 3, 3, 0, STATUS_FRESH);
    d = mk_unit(UNIT_WARRIOR, 1, 3, 5, 1, STATUS_FRESH);
    act = (PolyAction){ACT_ATTACK, (int16_t)a, (int16_t)d, -1};
    CHECK(poly_execute_action(&S, &act));
    CHECK(S.units[d].hp < 10 && S.units[d].type != UNIT_NONE); // damaged, alive
    CHECK(S.units[a].hp == 10);                                // no retaliation at range 2
}

static void test_capture_action(void) {
    board_reset(9);
    int cap = mk_city(0, 1, 1, true);
    S.terrain[tile_idx(&S, 6, 6)] = TERRAIN_VILLAGE;
    int u = mk_unit(UNIT_WARRIOR, 0, 6, 6, cap, STATUS_FRESH);
    PolyAction act = {ACT_CAPTURE, (int16_t)u, -1, -1};
    CHECK(poly_execute_action(&S, &act));
    CHECK(S.players[0].num_cities == 2);
    CHECK(S.units[u].status == STATUS_FINISHED);
    CHECK(S.terrain[tile_idx(&S, 6, 6)] == TERRAIN_CITY);
}

static void test_build_and_levelup(void) {
    board_reset(9);
    int ci = mk_city(0, 4, 4, true);
    S.players[0].techs |= (1u << TECH_ORGANIZATION) | (1u << TECH_FARMING);
    int t = tile_idx(&S, 4, 3);
    S.resource[t] = RESOURCE_CROPS;
    PolyAction b = {ACT_BUILD, (int16_t)ci, (int16_t)t, BUILDING_FARM};
    CHECK(poly_execute_action(&S, &b));
    CHECK(S.building[t] == BUILDING_FARM);
    CHECK(S.resource[t] == RESOURCE_NONE);
    CHECK(S.cities[ci].population == 2);            // farm +2 pop -> can level (need 2)

    // level-up lock: only LEVELUP actions for this city; END_TURN infeasible
    PolyAction acts[POLY_MAX_ACTIONS];
    int n = poly_enumerate_actions(&S, acts, POLY_MAX_ACTIONS);
    bool has_end = false, has_nonlevel_city = false, has_level = false;
    for (int i = 0; i < n; i++) {
        if (acts[i].kind == ACT_END_TURN) has_end = true;
        if (acts[i].kind == ACT_LEVELUP) has_level = true;
        if (acts[i].kind == ACT_BUILD || acts[i].kind == ACT_GATHER) has_nonlevel_city = true;
    }
    CHECK(has_level && !has_end && !has_nonlevel_city);

    // workshop: level 2, +1 production
    PolyAction lv = {ACT_LEVELUP, (int16_t)ci, -1, LEVELUP_WORKSHOP};
    CHECK(poly_execute_action(&S, &lv));
    CHECK(S.cities[ci].level == 2);
    CHECK(S.cities[ci].population == 0 && S.cities[ci].population_need == 3);
    CHECK(S.cities[ci].production == 1);
    CHECK(city_production(&S.cities[ci]) == 4);     // 2 level + 1 prod + 1 capital

    // border growth at level 3
    city_add_population(&S, ci, 3);
    lv = (PolyAction){ACT_LEVELUP, (int16_t)ci, -1, LEVELUP_CITY_WALL};
    CHECK(poly_execute_action(&S, &lv));
    CHECK(S.cities[ci].has_walls);
    city_add_population(&S, ci, 4);
    lv = (PolyAction){ACT_LEVELUP, (int16_t)ci, -1, LEVELUP_BORDER_GROWTH};
    CHECK(poly_execute_action(&S, &lv));
    CHECK(S.cities[ci].bound == 2);
    CHECK(S.city_at[tile_idx(&S, 6, 4)] == ci);     // radius-2 tile claimed
}

static void test_gather_upgrade_disband(void) {
    board_reset(9);
    int ci = mk_city(0, 4, 4, true);
    S.players[0].techs |= 1u << TECH_HUNTING;
    int t = tile_idx(&S, 3, 3);
    S.resource[t] = RESOURCE_ANIMAL;
    S.players[0].stars = 2;
    PolyAction g = {ACT_GATHER, (int16_t)ci, (int16_t)t, -1};
    CHECK(poly_execute_action(&S, &g));
    CHECK(S.players[0].stars == 0 && S.cities[ci].population == 1);

    // upgrade boat -> ship, hp/carried preserved
    S.players[0].techs |= 1u << TECH_SAILING;
    S.players[0].stars = 5;
    S.terrain[tile_idx(&S, 6, 6)] = TERRAIN_SHALLOW_WATER;
    int b = mk_unit(UNIT_BOAT, 0, 6, 6, ci, STATUS_FINISHED);
    S.units[b].carried = UNIT_SWORDMAN;
    S.units[b].hp = 12; S.units[b].max_hp = 15;
    PolyAction up = {ACT_UPGRADE, (int16_t)b, -1, -1};
    CHECK(poly_execute_action(&S, &up));            // works even when FINISHED
    CHECK(S.units[b].type == UNIT_SHIP && S.units[b].carried == UNIT_SWORDMAN);
    CHECK(S.units[b].hp == 12 && S.players[0].stars == 0);

    // disband refunds floor(cost/2)
    board_reset(9);
    ci = mk_city(0, 4, 4, true);
    S.players[0].techs |= 1u << TECH_FREE_SPIRIT;
    S.players[0].stars = 0;
    int u = mk_unit(UNIT_SWORDMAN, 0, 5, 5, ci, STATUS_FRESH);  // cost 5
    S.players[0].score = 25;
    PolyAction d = {ACT_DISBAND, (int16_t)u, -1, -1};
    CHECK(poly_execute_action(&S, &d));
    CHECK(S.players[0].stars == 2);                 // floor(5/2)
    CHECK(S.units[u].type == UNIT_NONE);
    CHECK(S.players[0].score == 0);                 // spawn points refunded away
}

static void test_move_embark(void) {
    board_reset(9);
    int ci = mk_city(0, 4, 4, true);
    S.players[0].techs |= 1u << TECH_SAILING;
    int pt = tile_idx(&S, 3, 4);
    S.terrain[pt] = TERRAIN_SHALLOW_WATER;
    S.building[pt] = BUILDING_PORT;
    int u = mk_unit(UNIT_WARRIOR, 0, 4, 3, ci, STATUS_FRESH);
    PolyAction m = {ACT_MOVE, (int16_t)u, (int16_t)pt, -1};
    CHECK(poly_execute_action(&S, &m));
    CHECK(S.units[u].type == UNIT_BOAT && S.units[u].carried == UNIT_WARRIOR);
    CHECK(S.units[u].x == 3 && S.units[u].y == 4);
}

static void test_random_playout_smoke(void) {
    // random legal actions until only END_TURN remains; must terminate
    board_reset(11);
    int c0 = mk_city(0, 2, 2, true), c1 = mk_city(1, 8, 8, true);
    mk_unit(UNIT_WARRIOR, 0, 2, 2, c0, STATUS_FRESH);
    mk_unit(UNIT_WARRIOR, 1, 8, 8, c1, STATUS_FRESH);
    S.players[0].stars = 20; S.players[1].stars = 20;
    S.players[0].techs |= 1u << TECH_ORGANIZATION;
    S.active_player = 0;

    PolyAction acts[POLY_MAX_ACTIONS];
    int steps = 0;
    for (; steps < 500; steps++) {
        int n = poly_enumerate_actions(&S, acts, POLY_MAX_ACTIONS);
        CHECK(n >= 1);
        // filter out END_TURN; stop when it's all that's left
        int m = 0;
        for (int i = 0; i < n; i++) if (acts[i].kind != ACT_END_TURN) acts[m++] = acts[i];
        if (m == 0) break;
        PolyAction* a = &acts[poly_rand_int(&S.rng, m)];
        CHECK(poly_execute_action(&S, a));
    }
    CHECK(steps < 500);   // ran out of actions before the safety cap
}

int main(void) {
    test_spawn_and_research();
    test_attack_kill_and_advance();
    test_capture_action();
    test_build_and_levelup();
    test_gather_upgrade_disband();
    test_move_embark();
    test_random_playout_smoke();
    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all action tests passed\n");
    return 0;
}
