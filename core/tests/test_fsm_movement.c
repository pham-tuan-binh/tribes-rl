// Tests for the unit turn-status FSM and movement/reachability,
// mirroring Unit.java and StepMove.java semantics.
#include <stdio.h>
#include <string.h>
#include "../polytopia.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

// --- helpers ---------------------------------------------------------------

static PolyState S;

static void board_reset(int size) {
    memset(&S, 0, sizeof(S));
    S.size = (int8_t)size;
    S.num_players = 2;
    for (int i = 0; i < MAX_TILES; i++) {
        S.terrain[i] = TERRAIN_PLAIN;
        S.resource[i] = RESOURCE_NONE;
        S.building[i] = BUILDING_NONE;
        S.unit_at[i] = -1;
        S.city_at[i] = -1;
    }
    // full visibility for both players (tests poke fog explicitly when needed)
    for (int p = 0; p < 2; p++)
        memset(S.players[p].obs, 0xff, sizeof(S.players[p].obs));
}

static int add_unit(UnitType type, int owner, int x, int y, TurnStatus st) {
    int i = S.num_units++;
    Unit* u = &S.units[i];
    memset(u, 0, sizeof(*u));
    u->type = (int8_t)type; u->owner = (int8_t)owner; u->carried = UNIT_NONE;
    u->x = (int8_t)x; u->y = (int8_t)y;
    u->max_hp = UNIT_STATS[type].max_hp; u->hp = u->max_hp;
    u->status = (int8_t)st; u->city = -1;
    S.unit_at[tile_idx(&S, x, y)] = (int16_t)i;
    return i;
}

// --- FSM --------------------------------------------------------------------

static void test_fsm_dash(void) {
    // Warrior: move then attack (Dash); attack always ends the turn.
    Unit u = {.type = UNIT_WARRIOR, .status = STATUS_FRESH};
    CHECK(unit_can_move(&u) && unit_can_attack(&u));
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_MOVED && !unit_can_move(&u) && unit_can_attack(&u));
    unit_transition(&u, STATUS_ATTACKED);
    CHECK(u.status == STATUS_FINISHED);
    // attack from FRESH ends turn immediately
    u.status = STATUS_FRESH;
    unit_transition(&u, STATUS_ATTACKED);
    CHECK(u.status == STATUS_FINISHED);
}

static void test_fsm_single_action(void) {
    // Defender/Catapult/MindBender: one action, then done.
    Unit u = {.type = UNIT_CATAPULT, .status = STATUS_FRESH};
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_FINISHED);
    u.status = STATUS_FRESH; u.type = UNIT_DEFENDER;
    unit_transition(&u, STATUS_ATTACKED);
    CHECK(u.status == STATUS_FINISHED);
    // SuperUnit validates like Dash but any transition finishes it.
    u.status = STATUS_FRESH; u.type = UNIT_SUPERUNIT;
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_FINISHED);
}

static void test_fsm_rider_escape(void) {
    // move -> attack -> move (escape) -> finished
    Unit u = {.type = UNIT_RIDER, .status = STATUS_FRESH};
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_MOVED);
    unit_transition(&u, STATUS_ATTACKED);
    CHECK(u.status == STATUS_MOVED_AND_ATTACKED && unit_can_move(&u) && !unit_can_attack(&u));
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_FINISHED);
    // attack first: FRESH -> ATTACKED -> (move) MOVED_AND_ATTACKED -> (move) FINISHED
    u.status = STATUS_FRESH;
    unit_transition(&u, STATUS_ATTACKED);
    CHECK(u.status == STATUS_ATTACKED && unit_can_move(&u));
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_MOVED_AND_ATTACKED);
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_FINISHED);
}

static void test_fsm_knight_persist(void) {
    Unit u = {.type = UNIT_KNIGHT, .status = STATUS_FRESH};
    unit_transition(&u, STATUS_MOVED);
    CHECK(u.status == STATUS_MOVED && unit_can_attack(&u));
    unit_transition(&u, STATUS_ATTACKED);        // attack, no kill
    CHECK(u.status == STATUS_FINISHED && !unit_can_attack(&u));
    // with a kill: addKill re-arms the attack
    u.status = STATUS_MOVED;
    unit_add_kill(&u);                            // kill during attack resolution
    CHECK(u.status == STATUS_ATTACKED && unit_can_attack(&u));
    unit_transition(&u, STATUS_ATTACKED);        // chain attack, no kill this time
    CHECK(u.status == STATUS_FINISHED);
}

static void test_fsm_pushed_and_finished(void) {
    // PUSHED units can only transition to FINISHED.
    Unit u = {.type = UNIT_WARRIOR, .status = STATUS_PUSHED};
    CHECK(!unit_can_move(&u) && !unit_can_attack(&u));
    CHECK(unit_can_transition(UNIT_WARRIOR, STATUS_PUSHED, STATUS_FINISHED));
    // FINISHED is absorbing.
    u.status = STATUS_FINISHED;
    CHECK(!unit_can_transition(UNIT_WARRIOR, STATUS_FINISHED, STATUS_FINISHED));
}

// --- movement ---------------------------------------------------------------

static uint8_t dests[MAX_TILES];

static void test_move_basic_range(void) {
    board_reset(7);
    int w = add_unit(UNIT_WARRIOR, 0, 3, 3, STATUS_FRESH);
    int cnt = poly_reachable(&S, w, dests);
    CHECK(cnt == 8);   // MOV 1: the 8 surrounding plains
    CHECK(dests[tile_idx(&S, 2, 2)] && dests[tile_idx(&S, 4, 4)]);
    CHECK(!dests[tile_idx(&S, 3, 3)]);   // not the start tile
    CHECK(!dests[tile_idx(&S, 5, 3)]);   // out of range

    // Rider MOV 2: Chebyshev disk radius 2 on open plains = 24 tiles
    board_reset(7);
    int r = add_unit(UNIT_RIDER, 0, 3, 3, STATUS_FRESH);
    cnt = poly_reachable(&S, r, dests);
    CHECK(cnt == 24);
}

static void test_move_forest_mountain_stop(void) {
    // Forest costs all remaining movement: rider entering adjacent forest stops there.
    board_reset(7);
    S.terrain[tile_idx(&S, 4, 3)] = TERRAIN_FOREST;
    int r = add_unit(UNIT_RIDER, 0, 3, 3, STATUS_FRESH);
    poly_reachable(&S, r, dests);
    CHECK(dests[tile_idx(&S, 4, 3)]);     // can enter the forest...
    CHECK(dests[tile_idx(&S, 5, 3)]);     // (5,3) still reachable AROUND via plain (4,2)/(4,4)
    // wall of forest: no going through
    board_reset(7);
    for (int y = 0; y < 7; y++) S.terrain[tile_idx(&S, 4, y)] = TERRAIN_FOREST;
    r = add_unit(UNIT_RIDER, 0, 3, 3, STATUS_FRESH);
    poly_reachable(&S, r, dests);
    CHECK(dests[tile_idx(&S, 4, 3)]);
    CHECK(!dests[tile_idx(&S, 5, 3)]);    // blocked: forest consumed all movement

    // Mountain impassable without CLIMBING; passable (stop) with it.
    board_reset(7);
    S.terrain[tile_idx(&S, 4, 3)] = TERRAIN_MOUNTAIN;
    int w = add_unit(UNIT_WARRIOR, 0, 3, 3, STATUS_FRESH);
    poly_reachable(&S, w, dests);
    CHECK(!dests[tile_idx(&S, 4, 3)]);
    S.players[0].techs |= 1u << TECH_CLIMBING;
    poly_reachable(&S, w, dests);
    CHECK(dests[tile_idx(&S, 4, 3)]);
}

static void test_move_water_and_ports(void) {
    board_reset(7);
    for (int y = 0; y < 7; y++)
        for (int x = 5; x < 7; x++) S.terrain[tile_idx(&S, x, y)] = TERRAIN_SHALLOW_WATER;
    int w = add_unit(UNIT_WARRIOR, 0, 4, 3, STATUS_FRESH);
    poly_reachable(&S, w, dests);
    CHECK(!dests[tile_idx(&S, 5, 3)]);    // no port: water is off-limits to ground units
    // port + SAILING: embark allowed
    S.building[tile_idx(&S, 5, 3)] = BUILDING_PORT;
    S.players[0].techs |= 1u << TECH_SAILING;
    poly_reachable(&S, w, dests);
    CHECK(dests[tile_idx(&S, 5, 3)]);
    CHECK(!dests[tile_idx(&S, 6, 3)]);    // embark consumes all movement

    // water unit: sails on water (cost 1), disembark consumes all movement
    board_reset(7);
    for (int y = 0; y < 7; y++)
        for (int x = 3; x < 7; x++) S.terrain[tile_idx(&S, x, y)] = TERRAIN_SHALLOW_WATER;
    S.players[0].techs |= 1u << TECH_SAILING;
    int b = add_unit(UNIT_BOAT, 0, 4, 3, STATUS_FRESH);   // MOV 2
    poly_reachable(&S, b, dests);
    CHECK(dests[tile_idx(&S, 6, 3)]);     // 2 water steps
    CHECK(dests[tile_idx(&S, 2, 3)]);     // disembark onto plain
    CHECK(!dests[tile_idx(&S, 1, 3)]);    // but no further
}

static void test_move_zoc_and_blockers(void) {
    board_reset(7);
    int r = add_unit(UNIT_RIDER, 0, 1, 3, STATUS_FRESH);    // MOV 2
    add_unit(UNIT_WARRIOR, 1, 3, 2, STATUS_FRESH);          // enemy
    poly_reachable(&S, r, dests);
    // (2,3) is adjacent to enemy at (3,2): ZoC consumes all movement there.
    CHECK(dests[tile_idx(&S, 2, 3)]);
    // Tiles only reachable THROUGH the ZoC tile at cost 1 then +1 are not reachable
    // unless another ZoC-free path exists within MOV. (4,3) via (2,3)->(3,3) both ZoC.
    CHECK(!dests[tile_idx(&S, 4, 3)]);
    // enemy tile itself is never enterable/pass-through
    CHECK(!dests[tile_idx(&S, 3, 2)]);

    // friendly units: pass-through OK, destination blocked
    board_reset(7);
    r = add_unit(UNIT_RIDER, 0, 1, 3, STATUS_FRESH);
    add_unit(UNIT_WARRIOR, 0, 2, 3, STATUS_FRESH);          // friend in the way
    poly_reachable(&S, r, dests);
    CHECK(!dests[tile_idx(&S, 2, 3)]);    // occupied by friend
    CHECK(dests[tile_idx(&S, 3, 3)]);     // but we passed through it
}

static void test_move_roads(void) {
    // Roads halve step cost (min 0.5): warrior MOV 1 on a road line reaches 2 tiles.
    board_reset(7);
    for (int x = 0; x < 7; x++) S.road[tile_idx(&S, x, 3)] = 1;
    int w = add_unit(UNIT_WARRIOR, 0, 1, 3, STATUS_FRESH);
    poly_reachable(&S, w, dests);
    CHECK(dests[tile_idx(&S, 2, 3)]);
    CHECK(dests[tile_idx(&S, 3, 3)]);     // 0.5 + 0.5 = 1.0 <= MOV
    CHECK(!dests[tile_idx(&S, 4, 3)]);    // 1.5 -> floor 1 <= 1 BUT expansion stops at cost==MOV
    // off-road neighbours still cost 1.0
    CHECK(dests[tile_idx(&S, 1, 2)]);
}

static void test_move_fog_gate(void) {
    board_reset(7);
    int w = add_unit(UNIT_WARRIOR, 0, 3, 3, STATUS_FRESH);
    // hide (4,3) from player 0
    S.players[0].obs[tile_idx(&S, 4, 3) >> 3] &= (uint8_t)~(1u << (tile_idx(&S, 4, 3) & 7));
    poly_reachable(&S, w, dests);
    CHECK(!dests[tile_idx(&S, 4, 3)]);
    CHECK(dests[tile_idx(&S, 4, 2)]);
}

int main(void) {
    test_fsm_dash();
    test_fsm_single_action();
    test_fsm_rider_escape();
    test_fsm_knight_persist();
    test_fsm_pushed_and_finished();
    test_move_basic_range();
    test_move_forest_mountain_stop();
    test_move_water_and_ports();
    test_move_zoc_and_blockers();
    test_move_roads();
    test_move_fog_gate();
    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all fsm/movement tests passed\n");
    return 0;
}
