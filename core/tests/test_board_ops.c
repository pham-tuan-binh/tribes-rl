// Board-ops tests: city economy, building effects, visibility, trade network,
// push, capture. Mirrors City.java / Tribe.java / Board.java / TradeNetwork.java.
#include <stdio.h>
#include <string.h>
#include "../polytopia.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static PolyState S;

static void board_reset(int size) {
    memset(&S, 0, sizeof(S));
    S.size = (int8_t)size;
    S.num_players = 2;
    S.rng = 12345;
    for (int i = 0; i < MAX_TILES; i++) {
        S.terrain[i] = TERRAIN_PLAIN;
        S.resource[i] = RESOURCE_NONE;
        S.building[i] = BUILDING_NONE;
        S.unit_at[i] = -1;
        S.city_at[i] = -1;
    }
    S.players[0].capital = -1; S.players[1].capital = -1;
    for (int p = 0; p < 2; p++)
        memset(S.players[p].obs, 0xff, sizeof(S.players[p].obs));
}

// minimal city creator for tests (bypasses capture path)
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

static int mk_unit(UnitType type, int owner, int x, int y, int city) {
    int ui = poly_new_unit(&S, type, owner, x, y);
    if (city >= 0) city_add_unit(&S, city, ui);
    return ui;
}

static void test_population_and_score(void) {
    board_reset(9);
    int ci = mk_city(0, 4, 4, true);
    int score0 = S.players[0].score;
    city_add_population(&S, ci, 3);
    CHECK(S.cities[ci].population == 3);
    CHECK(S.players[0].score == score0 + 15);            // 3 * 5 points
    CHECK(S.cities[ci].points_worth >= 15);
    // clamp at -level
    city_add_population(&S, ci, -10);
    CHECK(S.cities[ci].population == -1);                // level 1 -> floor -1
    // production short-circuits on negative pop
    CHECK(city_production(&S.cities[ci]) == -1);
}

static void test_building_effects_pairs(void) {
    // Farm at (3,3), then windmill adjacent at (4,3):
    //  - farm placement: +2 pop (its own bonus)
    //  - windmill placement: +1 pop per adjacent farm, credited to the FARM's city
    board_reset(9);
    int ci = mk_city(0, 3, 4, true);
    int t_farm = tile_idx(&S, 3, 3), t_mill = tile_idx(&S, 4, 3);
    S.city_at[t_farm] = (int8_t)ci; S.city_at[t_mill] = (int8_t)ci;

    S.building[t_farm] = BUILDING_FARM;
    city_building_effects(&S, ci, t_farm, BUILDING_FARM, false, false);
    CHECK(S.cities[ci].population == 2);

    S.building[t_mill] = BUILDING_WINDMILL;
    city_building_effects(&S, ci, t_mill, BUILDING_WINDMILL, false, false);
    CHECK(S.cities[ci].population == 3);                 // +1 from windmill next to 1 farm

    // Port: +2 pop direct; customs house next to it: +2 production
    board_reset(9);
    ci = mk_city(0, 3, 4, true);
    int t_port = tile_idx(&S, 2, 4);                     // in city borders (bound 1)
    S.terrain[t_port] = TERRAIN_SHALLOW_WATER;
    int t_cust = tile_idx(&S, 2, 3);
    CHECK(S.city_at[t_port] == ci && S.city_at[t_cust] == ci);
    S.building[t_port] = BUILDING_PORT;
    city_building_effects(&S, ci, t_port, BUILDING_PORT, false, false);
    CHECK(S.cities[ci].population == 2);
    S.building[t_cust] = BUILDING_CUSTOMS_HOUSE;
    city_building_effects(&S, ci, t_cust, BUILDING_CUSTOMS_HOUSE, false, false);
    CHECK(S.cities[ci].production == 2);
}

static void test_temple_scoring_quirk(void) {
    board_reset(9);
    int ci = mk_city(0, 4, 4, true);
    int t = tile_idx(&S, 4, 3);
    S.city_at[t] = (int8_t)ci;
    S.building[t] = BUILDING_TEMPLE;
    S.temple_level[t] = 1;
    int score0 = S.players[0].score;
    city_building_effects(&S, ci, t, BUILDING_TEMPLE, false, false);
    CHECK(S.players[0].score == score0 + 100 + 5);       // +100 temple, +5 for 1 pop
    // Java quirk: destroying a temple ADDS its accumulated points
    int score1 = S.players[0].score;
    city_building_effects(&S, ci, t, BUILDING_TEMPLE, true, false);
    CHECK(S.players[0].score == score1 + 100 - 5);       // +getPoints(level 1)=100, -5 pop
}

static void test_clear_view(void) {
    board_reset(9);
    memset(S.players[0].obs, 0, sizeof(S.players[0].obs));   // all fog
    int score0 = S.players[0].score;
    bool net = poly_clear_view(&S, 0, 4, 4, 1);
    CHECK(!net);                                          // no roads/water revealed
    CHECK(S.players[0].score == score0 + 9 * CLEAR_VIEW_POINTS);
    CHECK(obs_get(&S.players[0], tile_idx(&S, 3, 3)));
    CHECK(!obs_get(&S.players[0], tile_idx(&S, 6, 4)));
    // revealing water triggers a network update flag
    S.terrain[tile_idx(&S, 6, 4)] = TERRAIN_SHALLOW_WATER;
    net = poly_clear_view(&S, 0, 6, 4, 0);
    CHECK(net);
}

static void test_trade_network_roads(void) {
    // capital at (1,1), second city at (5,1); connect with roads on y=1
    board_reset(9);
    int cap = mk_city(0, 1, 1, true);
    int c2 = mk_city(0, 5, 1, false);
    (void)cap;
    poly_network_update_player(&S, 0, true);
    CHECK(S.players[0].connected_cities == 0);           // not connected yet
    int pop_cap0 = S.cities[cap].population, pop_c2_0 = S.cities[c2].population;
    // roads at (3,1) — gap remains at... 2..4 needed: city borders reach (2,1) and (4,1)
    // but borders are not roads; net tiles must bridge city centers.
    S.net_tile[tile_idx(&S, 2, 1)] = 1;
    S.net_tile[tile_idx(&S, 3, 1)] = 1;
    S.net_tile[tile_idx(&S, 4, 1)] = 1;
    poly_network_update_player(&S, 0, true);
    CHECK(S.players[0].connected_cities == (1ull << c2));
    CHECK(S.cities[cap].population == pop_cap0 + 1);     // capital +1
    CHECK(S.cities[c2].population == pop_c2_0 + 1);      // connected city +1 (own turn)
    // breaking the road disconnects: -1 pop both
    S.net_tile[tile_idx(&S, 3, 1)] = 0;
    poly_network_update_player(&S, 0, true);
    CHECK(S.players[0].connected_cities == 0);
    CHECK(S.cities[cap].population == pop_cap0);
    CHECK(S.cities[c2].population == pop_c2_0);
}

static void test_trade_network_ports(void) {
    // two cities, each with a port, 3 water tiles apart -> linked (<= 4)
    board_reset(12);
    int cap = mk_city(0, 2, 2, true);
    int c2 = mk_city(0, 8, 2, false);
    for (int x = 3; x <= 7; x++) S.terrain[tile_idx(&S, x, 2)] = TERRAIN_SHALLOW_WATER;
    int p1 = tile_idx(&S, 3, 2), p2 = tile_idx(&S, 7, 2);
    S.city_at[p1] = (int8_t)cap;                          // ports inside own borders
    S.city_at[p2] = (int8_t)c2;
    S.building[p1] = BUILDING_PORT; S.net_tile[p1] = 1;
    S.building[p2] = BUILDING_PORT; S.net_tile[p2] = 1;
    poly_network_update_player(&S, 0, true);
    CHECK(S.players[0].connected_cities == (1ull << c2));
    // too far: move second port one tile further (5 water steps)
    board_reset(12);
    cap = mk_city(0, 2, 2, true);
    c2 = mk_city(0, 9, 2, false);
    for (int x = 3; x <= 8; x++) S.terrain[tile_idx(&S, x, 2)] = TERRAIN_SHALLOW_WATER;
    p1 = tile_idx(&S, 3, 2); p2 = tile_idx(&S, 8, 2);
    S.city_at[p1] = (int8_t)cap; S.city_at[p2] = (int8_t)c2;
    S.building[p1] = BUILDING_PORT; S.net_tile[p1] = 1;
    S.building[p2] = BUILDING_PORT; S.net_tile[p2] = 1;
    poly_network_update_player(&S, 0, true);
    CHECK(S.players[0].connected_cities == 0);
}

static void test_push(void) {
    board_reset(9);
    mk_city(0, 4, 4, true);
    int u = mk_unit(UNIT_WARRIOR, 0, 4, 4, 0);
    // South (dy=+1) is tried first
    CHECK(poly_push_unit(&S, u));
    CHECK(S.units[u].x == 4 && S.units[u].y == 5);
    CHECK(S.units[u].status == STATUS_PUSHED);
    // blocked south -> west next
    board_reset(9);
    mk_city(0, 4, 4, true);
    u = mk_unit(UNIT_WARRIOR, 0, 4, 4, 0);
    mk_unit(UNIT_WARRIOR, 0, 4, 5, -1);
    CHECK(poly_push_unit(&S, u));
    CHECK(S.units[u].x == 3 && S.units[u].y == 4);
    // fully surrounded and no options -> not pushed (caller removes it)
    board_reset(9);
    mk_city(0, 4, 4, true);
    u = mk_unit(UNIT_WARRIOR, 0, 4, 4, 0);
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
        if (dx || dy) mk_unit(UNIT_WARRIOR, 0, 4 + dx, 4 + dy, -1);
    CHECK(!poly_push_unit(&S, u));
}

static void test_capture_village(void) {
    board_reset(9);
    int cap = mk_city(0, 2, 2, true);
    S.terrain[tile_idx(&S, 6, 6)] = TERRAIN_VILLAGE;
    int uw = mk_unit(UNIT_WARRIOR, 0, 6, 6, cap);
    (void)uw;
    int score0 = S.players[0].score;
    int ci = poly_capture(&S, 0, 6, 6);
    CHECK(ci >= 0);
    CHECK(S.terrain[tile_idx(&S, 6, 6)] == TERRAIN_CITY);
    CHECK(S.cities[ci].owner == 0 && S.cities[ci].level == 1);
    CHECK(S.city_at[tile_idx(&S, 5, 5)] == ci);          // border claimed
    CHECK(S.players[0].num_cities == 2);
    // unit moved from capital's roster to the new city
    CHECK(S.cities[cap].num_units == 0 && S.cities[ci].num_units == 1);
    CHECK(S.units[uw].city == ci);
    // score: centre + borders (9 tiles * 20) + ...
    CHECK(S.players[0].score >= score0 + CITY_CENTRE_POINTS + 9 * CITY_BORDER_POINTS);
}

static void test_capture_city_and_loss(void) {
    board_reset(9);
    int cap0 = mk_city(0, 2, 2, true);
    (void)cap0;
    int cap1 = mk_city(1, 6, 6, true);   // player 1's only city
    int u1 = mk_unit(UNIT_WARRIOR, 1, 6, 5, cap1);
    int u0 = mk_unit(UNIT_WARRIOR, 0, 6, 6, cap0);       // attacker on the city tile
    (void)u0;
    int ci = poly_capture(&S, 0, 6, 6);
    CHECK(ci == cap1);
    CHECK(S.cities[ci].owner == 0);
    CHECK(S.players[0].num_cities == 2 && S.players[1].num_cities == 0);
    // player 1 lost their last city: loss + all units wiped
    CHECK(S.players[1].result == RESULT_LOSS);
    CHECK(S.units[u1].type == UNIT_NONE);
    CHECK(S.unit_at[tile_idx(&S, 6, 5)] == -1);
}

int main(void) {
    test_population_and_score();
    test_building_effects_pairs();
    test_temple_scoring_quirk();
    test_clear_view();
    test_trade_network_roads();
    test_trade_network_ports();
    test_push();
    test_capture_village();
    test_capture_city_and_loss();
    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all board-ops tests passed\n");
    return 0;
}
