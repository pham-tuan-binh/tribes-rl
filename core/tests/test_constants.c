// M0 tests: table integrity + pure rules math vs hand-computed Java values.
#include <stdio.h>
#include <stdlib.h>
#include "../polytopia.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_tech_tree(void) {
    // 5 tier-1, 9 tier-2, 10 tier-3; parents one tier below
    int tiers[4] = {0, 0, 0, 0};
    for (int t = 0; t < NUM_TECH; t++) {
        TechInfo ti = TECH_INFO[t];
        CHECK(ti.tier >= 1 && ti.tier <= 3);
        tiers[ti.tier]++;
        if (ti.tier == 1) CHECK(ti.parent == TECH_NONE);
        else {
            CHECK(ti.parent >= 0 && ti.parent < NUM_TECH);
            CHECK(TECH_INFO[ti.parent].tier == ti.tier - 1);
        }
    }
    CHECK(tiers[1] == 5);
    CHECK(tiers[2] == 10);   // Types.java:30-39
    CHECK(tiers[3] == 9);    // Types.java:40-48

    // Cost formula: base 4 + tier*cities; PHILOSOPHY discount ×0.2 truncated.
    CHECK(tech_cost(TECH_CLIMBING, 1, false) == 5);       // 4 + 1*1
    CHECK(tech_cost(TECH_FARMING, 3, false) == 10);       // 4 + 2*3
    CHECK(tech_cost(TECH_PHILOSOPHY, 2, false) == 10);    // 4 + 3*2
    CHECK(tech_cost(TECH_FARMING, 3, true) == 2);         // (int)(10*0.2)
    CHECK(tech_cost(TECH_CLIMBING, 1, true) == 1);        // (int)(5*0.2)
}

static void test_unit_stats(void) {
    // Spot-check against TribesConfig.java
    CHECK(UNIT_STATS[UNIT_WARRIOR].atk == 2 && UNIT_STATS[UNIT_WARRIOR].max_hp == 10);
    CHECK(UNIT_STATS[UNIT_CATAPULT].def == 0 && UNIT_STATS[UNIT_CATAPULT].range == 3);
    CHECK(UNIT_STATS[UNIT_MIND_BENDER].atk == 0);
    CHECK(UNIT_STATS[UNIT_KNIGHT].mov == 3 && UNIT_STATS[UNIT_KNIGHT].cost == 8);
    CHECK(UNIT_STATS[UNIT_SUPERUNIT].max_hp == 40);
    CHECK(UNIT_STATS[UNIT_BATTLESHIP].cost == 15 && UNIT_STATS[UNIT_BATTLESHIP].water);
    int n_spawnable = 0, n_water = 0, n_ranged = 0, n_fortify = 0;
    for (int u = 0; u < NUM_UNIT; u++) {
        n_spawnable += UNIT_STATS[u].spawnable;
        n_water += UNIT_STATS[u].water;
        n_ranged += UNIT_STATS[u].ranged;
        n_fortify += UNIT_STATS[u].fortify;
    }
    CHECK(n_spawnable == 8);   // all but boat/ship/battleship/superunit
    CHECK(n_water == 3);
    CHECK(n_ranged == 5);      // archer, catapult, boat, ship, battleship
    CHECK(n_fortify == 6);     // warrior, rider, archer, defender, swordman, knight
}

static void test_buildings(void) {
    int n_base = 0, n_temple = 0, n_monument = 0;
    for (int b = 0; b < NUM_BUILDING; b++) {
        n_base += BUILDING_INFO[b].is_base;
        n_temple += BUILDING_INFO[b].is_temple;
        n_monument += BUILDING_INFO[b].is_monument;
        if (BUILDING_INFO[b].is_monument) {
            CHECK(BUILDING_INFO[b].cost == 0 && BUILDING_INFO[b].bonus == 3);
            CHECK(b >= 12 && b <= 18);
        }
    }
    CHECK(n_base == 3 && n_temple == 4 && n_monument == 7);
    CHECK(BUILDING_INFO[BUILDING_PORT].cost == 10);
    CHECK(BUILDING_INFO[BUILDING_LUMBER_HUT].cost == 2);
    CHECK(BUILDING_INFO[BUILDING_FOREST_TEMPLE].cost == 15);   // cheaper than other temples
    CHECK(BUILDING_INFO[BUILDING_TEMPLE].cost == 20);
    // pair symmetry
    for (int b = 0; b < NUM_BUILDING; b++) {
        BuildingType p = building_pair((BuildingType)b);
        if (p != BUILDING_NONE) CHECK(building_pair(p) == (BuildingType)b);
    }
    CHECK(BUILDING_INFO[BUILDING_MINE].resource_req == RESOURCE_ORE);
    CHECK(BUILDING_INFO[BUILDING_FARM].resource_req == RESOURCE_CROPS);
    CHECK(BUILDING_INFO[BUILDING_SAWMILL].adj_req == BUILDING_LUMBER_HUT);
}

static void test_tribes(void) {
    CHECK(TRIBE_INFO[TRIBE_LUXIDOOR].initial_tech == TECH_NONE);
    CHECK(TRIBE_INFO[TRIBE_OUMAJI].starting_unit == UNIT_RIDER);
    CHECK(TRIBE_INFO[TRIBE_QUETZALI].starting_unit == UNIT_DEFENDER);
    CHECK(TRIBE_INFO[TRIBE_VENGIR].starting_unit == UNIT_SWORDMAN);
    CHECK(TRIBE_INFO[TRIBE_HOODRICK].starting_unit == UNIT_ARCHER);
}

static void test_combat(void) {
    // Full-HP warrior vs full-HP warrior, no bonus:
    // AF=2, DF=2, total=4 -> round(0.5 * 2 * 4.5) = round(4.5) = 5 both ways
    // (matches real Polytopia: full-HP warriors trade 5 damage).
    CombatResult r = combat_results(2, 10, 10, 2, 10, 10, 1.0);
    CHECK(r.to_target == 5 && r.to_attacker == 5);

    // Swordman (3/3, 15HP full) vs half-HP warrior (def 2, 5/10), no bonus:
    // AF=3, DF=2*0.5=1, total=4 -> to_target = round(3/4*3*4.5) = round(10.125) = 10 (kill)
    //                              to_attacker = round(1/4*2*4.5) = round(2.25) = 2
    r = combat_results(3, 15, 15, 2, 5, 10, 1.0);
    CHECK(r.to_target == 10 && r.to_attacker == 2);

    // Warrior vs defender (def 3, 15HP) behind walls (×4.0):
    // AF=2, DF=3*4=12, total=14 -> to_target = round(2/14*2*4.5) = round(1.2857) = 1
    //                              to_attacker = round(12/14*3*4.5) = round(11.571) = 12
    r = combat_results(2, 10, 10, 3, 15, 15, 4.0);
    CHECK(r.to_target == 1 && r.to_attacker == 12);

    // Catapult (atk 4, def 0) attacked by warrior: defence force 0 -> full modifier dmg.
    // AF=2, DF=0, total=2 -> to_target = round(1 * 2 * 4.5) = 9; retaliation = 0.
    r = combat_results(2, 10, 10, 0, 10, 10, 1.0);
    CHECK(r.to_target == 9 && r.to_attacker == 0);

    // Mind bender (atk 0) cannot deal damage; guard division by zero when def also 0.
    r = combat_results(0, 10, 10, 0, 10, 10, 1.0);
    CHECK(r.to_target == 0 && r.to_attacker == 0);

    // java_round semantics: floor(x+0.5)
    CHECK(java_round(2.5) == 3 && java_round(2.49) == 2 && java_round(11.571428) == 12);
}

static void test_city_econ(void) {
    City c = {0};
    c.level = 2; c.production = 1; c.is_capital = true; c.population = 0;
    CHECK(city_production(&c) == 4);   // level 2 + prod 1 + capital 1
    c.population = -2;
    CHECK(city_production(&c) == -2);  // negative pop short-circuits
    CHECK(levelup_points(1) == 100);
    CHECK(levelup_points(2) == 40);    // 50 - 2*5
    CHECK(levelup_points(4) == 30);
}

static void test_misc_tables(void) {
    CHECK(DEFAULT_MAP_SIZE[1] == 11 && DEFAULT_MAP_SIZE[7] == 24);
    CHECK(TEMPLE_POINTS[0] == 100 && TEMPLE_POINTS[4] == 150);
    CHECK(RESOURCE_INFO[RESOURCE_WHALES].star_bonus == 10);
    CHECK(RESOURCE_INFO[RESOURCE_FRUIT].pop_bonus == 1 && RESOURCE_INFO[RESOURCE_FRUIT].cost == 2);
    CHECK(RESOURCE_INFO[RESOURCE_RUINS].tech_req == TECH_NONE);
}

int main(void) {
    test_tech_tree();
    test_unit_stats();
    test_buildings();
    test_tribes();
    test_combat();
    test_city_econ();
    test_misc_tables();
    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all constants/math tests passed\n");
    return 0;
}
