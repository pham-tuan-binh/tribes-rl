// RL wrapper tests: masked random self-play through the SELECT/VERB/TARGET
// phase machine. Invariants: the active agent always has a nonzero mask,
// masked choices always apply cleanly, episodes terminate, rewards are ±1.
#include <stdio.h>
#include <string.h>
#include "../../puffer/polytopia_env.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static uint8_t obs_buf[ENV_PLAYERS][OBS_SIZE];
static float act_buf[ENV_PLAYERS];
static float rew_buf[ENV_PLAYERS];
static float term_buf[ENV_PLAYERS];
static unsigned char mask_buf[ENV_PLAYERS][ACTION_N];

static PolyEnv E;

static void env_setup(uint32_t seed) {
    memset(&E, 0, sizeof(E));
    E.observations = &obs_buf[0][0];
    E.actions = act_buf;
    E.rewards = rew_buf;
    E.terminals = term_buf;
    E.action_mask = &mask_buf[0][0];
    E.num_agents = ENV_PLAYERS;
    E.rng = seed;
    poly_env_reset(&E);   // default wiring fills the per-slot pointers
}

static int pick_masked(uint32_t* rng, const unsigned char* mask) {
    int legal[ACTION_N], n = 0;
    for (int i = 0; i < ACTION_N; i++) if (mask[i]) legal[n++] = i;
    if (n == 0) return -1;
    return legal[poly_rand_int(rng, n)];
}

static void test_action_space_constants(void) {
    CHECK(ACTION_N == 121 + 76);
    CHECK(V_DESTROY == VERB_N - 1);
#if ENV_PLAYERS == 2
    CHECK(OBS_SIZE == 10 * 121 + 4 + 2 * (5 + 24));
#endif
    // verb slots must be disjoint & in range
    CHECK(V_RESEARCH0 + NUM_TECH - 1 < V_BUILD_ROAD + 1);
    CHECK(V_UNIT0 + 9 < V_BUILD0);
    CHECK(V_BUILD0 + NUM_BUILDING - 1 < V_SPAWN0);
    CHECK(V_SPAWN0 + 7 < V_LEVELUP0);
    CHECK(V_LEVELUP0 + NUM_LEVELUP - 1 < V_GATHER);
}

static void test_initial_state(void) {
    env_setup(7);
    CHECK(E.game.active_player == 0 && E.phase == PH_SELECT);
    // active agent has choices; waiting agent can only PASS
    int n0 = 0, n1 = 0;
    for (int i = 0; i < ACTION_N; i++) { n0 += mask_buf[0][i]; n1 += mask_buf[1][i]; }
    CHECK(n0 >= 2);                          // at least END_TURN + something
    CHECK(n1 == 1 && mask_buf[1][ENV_TILES + V_END_TURN] == 1);
    // END_TURN is legal at SELECT
    CHECK(mask_buf[0][ENV_TILES + V_END_TURN] == 1);
    // obs: own capital marked (plane 4 value 10+level) somewhere for each agent
    bool own_cap[2] = {false, false};
    for (int a = 0; a < 2; a++)
        for (int t = 0; t < ENV_TILES; t++)
            if (obs_buf[a][4 * ENV_TILES + t] >= 10 && obs_buf[a][4 * ENV_TILES + t] < 20)
                own_cap[a] = true;
    CHECK(own_cap[0] && own_cap[1]);
}

static void test_full_random_episodes(void) {
    uint32_t pick_rng = 123;
    env_setup(977u);   // one env; episodes flow through auto-reset
    for (int episode = 1; episode <= 10; episode++) {
        long steps = 0;
        bool done = false;
        while (!done && steps < 60000) {
            int active = E.game.active_player;
            // invariant: active agent's mask is nonzero
            int a = pick_masked(&pick_rng, mask_buf[active]);
            CHECK(a >= 0);
            // bias to END_TURN at SELECT so games progress
            if (E.phase == PH_SELECT && poly_rand_int(&pick_rng, 6) == 0 &&
                mask_buf[active][ENV_TILES + V_END_TURN])
                a = ENV_TILES + V_END_TURN;
            act_buf[active] = (float)a;
            for (int q = 0; q < ENV_PLAYERS; q++)
                if (q != active) act_buf[q] = (float)(ENV_TILES + V_END_TURN);   // PASS
            poly_env_step(&E);
            steps++;
            if (term_buf[0] > 0.5f) {
                done = true;
                CHECK(term_buf[1] > 0.5f);
                // Might rules: either a domination win (+1/-1) or a draw (0/0)
                bool win = (rew_buf[0] > 0.5f && rew_buf[1] < -0.5f) ||
                           (rew_buf[1] > 0.5f && rew_buf[0] < -0.5f);
                bool draw = rew_buf[0] == 0.0f && rew_buf[1] == 0.0f &&
                            E.final_result[0] == RESULT_INCOMPLETE;
                CHECK(win || draw);
            }
            if (failures) { fprintf(stderr, "  (ep %d, step %ld)\n", episode, steps); return; }
        }
        CHECK(done);
        // env auto-reset: fresh game with legal actions again
        int n0 = 0;
        for (int i = 0; i < ACTION_N; i++) n0 += mask_buf[E.game.active_player][i];
        CHECK(n0 >= 2 && !E.game.game_over);
    }
    CHECK(E.log.n >= 10.0f);
    CHECK(E.log.episode_length / E.log.n > 50.0f);   // non-trivial games
}

static void test_phase_walkthrough(void) {
    // drive one explicit unit move: SELECT own unit -> VERB move -> TARGET tile
    env_setup(99);
    int active = E.game.active_player;
    // find a selectable tile holding one of our units
    int sel = -1;
    for (int t = 0; t < ENV_TILES && sel < 0; t++) {
        int16_t ui = E.game.unit_at[t];
        if (mask_buf[active][t] && ui >= 0 && E.game.units[ui].owner == active) sel = t;
    }
    CHECK(sel >= 0);
    act_buf[active] = (float)sel;
    for (int q = 0; q < ENV_PLAYERS; q++)
        if (q != active) act_buf[q] = (float)(ENV_TILES + V_END_TURN);
    poly_env_step(&E);
    CHECK(E.phase == PH_VERB);
    CHECK(mask_buf[active][ENV_TILES + V_UNIT0 + 0]);   // MOVE offered
    // selection plane marks the tile
    CHECK(obs_buf[active][9 * ENV_TILES + sel] == 1);

    act_buf[active] = (float)(ENV_TILES + V_UNIT0 + 0);  // MOVE
    poly_env_step(&E);
    CHECK(E.phase == PH_TARGET);
    int dest = pick_masked(&E.rng, mask_buf[active]);
    CHECK(dest >= 0 && dest < ENV_TILES);
    act_buf[active] = (float)dest;
    poly_env_step(&E);
    CHECK(E.phase == PH_SELECT);
    int16_t ui = E.game.unit_at[dest];
    CHECK(ui >= 0 && E.game.units[ui].owner == active);  // unit arrived
}

int main(void) {
    test_action_space_constants();
    test_initial_state();
    test_phase_walkthrough();
    test_full_random_episodes();
    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all rl-env tests passed\n");
    return 0;
}
