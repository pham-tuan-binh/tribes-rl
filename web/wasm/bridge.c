// polytopia-rl — WASM bridge: the JS-facing C ABI for the website.
// Wraps the same PolyEnv the RL agents train in, so human play, agent
// spectating and training are all the exact same simulation.
//
// JS drives one micro-decision per call, mirroring the RL loop:
//   poly_new_game(seed, num_players) -> fresh game
//   poly_mask(player) -> ptr to ACTION_N bytes (legal micro-actions)
//   poly_act(action)  -> apply active player's micro-action
//   poly_obs(player)  -> ptr to OBS_SIZE bytes (for agent inference in JS)
//   plus direct state accessors for rendering.
#include <emscripten/emscripten.h>
#include <stdlib.h>
#include "../../puffer/polytopia_env.h"
#include "puffernet.h"   // vendored from PufferLib (MIT) — C inference for the trained policy

static PolyEnv E;
static void nets_rebuild(void);   // defined with the inference section below
static uint8_t obs_buf[ENV_PLAYERS][POLY_OBS_SIZE];
static float act_buf[ENV_PLAYERS];
static float rew_buf[ENV_PLAYERS];
static float term_buf[ENV_PLAYERS];
static unsigned char mask_buf[ENV_PLAYERS][ACTION_N];

EMSCRIPTEN_KEEPALIVE int poly_action_n(void) { return ACTION_N; }
EMSCRIPTEN_KEEPALIVE int poly_obs_size(void) { return POLY_OBS_SIZE; }
EMSCRIPTEN_KEEPALIVE int poly_map_size(void) { return ENV_SIZE; }
EMSCRIPTEN_KEEPALIVE int poly_num_players(void) { return E.game.num_players; }

EMSCRIPTEN_KEEPALIVE void poly_new_game(unsigned seed, int players, int fog) {
    memset(&E, 0, sizeof(E));
    E.observations = &obs_buf[0][0];
    E.actions = act_buf;
    E.rewards = rew_buf;
    E.terminals = term_buf;
    E.action_mask = &mask_buf[0][0];
    E.num_agents = ENV_PLAYERS;
    E.fog = fog;
    // one WASM module for every player count: the obs are player-agnostic
    if (players < 2) players = 2;
    if (players > ENV_PLAYERS) players = ENV_PLAYERS;
    E.min_players = players;
    E.max_players = players;
    E.max_episode_steps = 1 << 30;   // product rules: no practical stop
    E.rng = seed ? seed : 1;
    poly_env_reset(&E);
    nets_rebuild();   // fresh recurrent state per game
}

// per-player visibility for fog rendering (1 = tile explored)
EMSCRIPTEN_KEEPALIVE int poly_visible(int player, int tile) {
    return E.game.fog ? (obs_get(&E.game.players[player], tile) ? 1 : 0) : 1;
}
EMSCRIPTEN_KEEPALIVE int poly_fog_enabled(void) { return E.game.fog ? 1 : 0; }

// ---------------------------------------------------------------------------
// Trained-policy inference (puffernet). Weights = the native trainer's .bin
// checkpoint (flat float32, tensors 16-byte aligned): encoder w, decoder w,
// then MinGRU projections. One net per seat (independent recurrent state).
// ---------------------------------------------------------------------------
static Weights* WEIGHTS = NULL;
static PufferNet* NET[ENV_PLAYERS];
static uint32_t agent_rng = 0xC0FFEE;
static float agent_temperature = 0.5f;   // <1 suppresses low-probability junk actions
static int net_hidden = 0, net_layers = 0;

// exact float count of a checkpoint for a given architecture (all tensor
// sizes here are multiples of 8, so the 16-byte alignment adds no padding)
static int arch_floats(int hidden, int layers) {
    return hidden * POLY_OBS_SIZE + (ACTION_N + 1) * hidden + layers * 3 * hidden * hidden;
}

static void nets_rebuild(void) {
    if (!WEIGHTS) return;
    // auto-detect architecture from the checkpoint size
    const int H[] = {512, 768, 1024};
    const int L[] = {3, 4, 3};
    net_hidden = 0;
    for (int c = 0; c < 3; c++)
        if (arch_floats(H[c], L[c]) == WEIGHTS->size - 7) { net_hidden = H[c]; net_layers = L[c]; }
    if (!net_hidden) { net_hidden = 512; net_layers = 3; }   // last resort
    int logit_sizes[1] = {ACTION_N};
    for (int s = 0; s < ENV_PLAYERS; s++) {
        if (NET[s]) free_puffernet(NET[s]);
        WEIGHTS->idx = 0;
        NET[s] = make_puffernet(WEIGHTS, 1, POLY_OBS_SIZE, net_hidden, net_layers, logit_sizes, 1);
    }
}

EMSCRIPTEN_KEEPALIVE void poly_set_temperature(float t) { agent_temperature = t > 0.05f ? t : 0.05f; }

// JS: buf = poly_weights_alloc(nbytes); HEAPU8.set(binData, buf); poly_weights_load(nbytes)
static uint8_t* weights_staging = NULL;
EMSCRIPTEN_KEEPALIVE uint8_t* poly_weights_alloc(int nbytes) {
    free(weights_staging);
    weights_staging = (uint8_t*)malloc((size_t)nbytes);
    return weights_staging;
}
EMSCRIPTEN_KEEPALIVE int poly_weights_load(int nbytes) {
    size_t num_floats = (size_t)nbytes / sizeof(float);
    free(WEIGHTS);
    WEIGHTS = (Weights*)calloc(1, sizeof(Weights) + (num_floats + 7) * sizeof(float));
    WEIGHTS->data = (float*)(WEIGHTS + 1);
    memcpy(WEIGHTS->data, weights_staging, num_floats * sizeof(float));
    WEIGHTS->size = (int)num_floats + 7;
    free(weights_staging);
    weights_staging = NULL;
    nets_rebuild();
    return NET[0] != NULL;
}
EMSCRIPTEN_KEEPALIVE int poly_has_agent(void) { return WEIGHTS != NULL; }

// Forward pass for one seat with the CURRENT obs + mask; masked softmax
// sample over the policy logits. Returns an action index, or -1 if no weights.
EMSCRIPTEN_KEEPALIVE int poly_agent_act(int player) {
    if (!WEIGHTS || !NET[player]) return -1;
    PufferNet* net = NET[player];
    for (int i = 0; i < POLY_OBS_SIZE; i++)
        net->obs[i] = (float)obs_buf[player][i];   // raw bytes, as in training
    linear(net->encoder, net->obs);
    mingru(net->mingru, net->encoder->output);
    linear(net->decoder, net->mingru->output);
    const float* logits = net->decoder->output;    // [0..ACTION_N), value at [ACTION_N]
    const unsigned char* mask = mask_buf[player];

    float mx = -1e30f;
    for (int i = 0; i < ACTION_N; i++)
        if (mask[i] && logits[i] > mx) mx = logits[i];
    if (mx == -1e30f) return -1;
    float sum = 0.0f, probs[ACTION_N];
    for (int i = 0; i < ACTION_N; i++) {
        probs[i] = mask[i] ? expf((logits[i] - mx) / agent_temperature) : 0.0f;
        sum += probs[i];
    }
    float r = (float)poly_rand(&agent_rng) / 2147483648.0f * sum;
    for (int i = 0; i < ACTION_N; i++) {
        r -= probs[i];
        if (mask[i] && r <= 0.0f) return i;
    }
    for (int i = ACTION_N - 1; i >= 0; i--) if (mask[i]) return i;
    return -1;
}

// Apply one micro-action for the active player. Returns 1 while the game is
// running, 0 once it ended (the env auto-resets; call poly_new_game to control
// the next seed instead).
EMSCRIPTEN_KEEPALIVE int poly_act(int action) {
    int player = E.game.active_player;
    for (int p = 0; p < ENV_PLAYERS; p++)
        act_buf[p] = (float)(p == player ? action : ENV_TILES + V_END_TURN);  // others PASS
    poly_env_step(&E);
    return term_buf[0] > 0.5f ? 0 : 1;
}

EMSCRIPTEN_KEEPALIVE void poly_cancel(void) { poly_env_cancel(&E); }
EMSCRIPTEN_KEEPALIVE const unsigned char* poly_mask(int player) { return mask_buf[player]; }
EMSCRIPTEN_KEEPALIVE const uint8_t* poly_obs(int player) { return &obs_buf[player][0]; }

// --- rendering accessors (direct engine planes; JS reads via HEAP views) ---
EMSCRIPTEN_KEEPALIVE const int8_t* poly_terrain(void) { return E.game.terrain; }
EMSCRIPTEN_KEEPALIVE const int8_t* poly_resource(void) { return E.game.resource; }
EMSCRIPTEN_KEEPALIVE const int8_t* poly_building(void) { return E.game.building; }
EMSCRIPTEN_KEEPALIVE const uint8_t* poly_roads(void) { return E.game.net_tile; }
EMSCRIPTEN_KEEPALIVE const int8_t* poly_city_at(void) { return E.game.city_at; }

EMSCRIPTEN_KEEPALIVE int poly_active_player(void) { return E.game.active_player; }
EMSCRIPTEN_KEEPALIVE int poly_phase(void) { return E.phase; }
EMSCRIPTEN_KEEPALIVE int poly_selected_tile(void) { return E.sel_tile; }
EMSCRIPTEN_KEEPALIVE int poly_tick(void) { return E.game.tick; }
EMSCRIPTEN_KEEPALIVE int poly_game_over(void) { return E.game.game_over ? 1 : 0; }
// result of the LAST FINISHED game (the env auto-resets on episode end)
EMSCRIPTEN_KEEPALIVE int poly_result(int player) { return E.final_result[player]; }
// LIVE status of the running game: 1 while the player is eliminated (RESULT_LOSS)
EMSCRIPTEN_KEEPALIVE int poly_eliminated(int player) { return E.game.players[player].result == RESULT_LOSS ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int poly_stars(int player) { return E.game.players[player].stars; }
EMSCRIPTEN_KEEPALIVE int poly_score(int player) { return E.game.players[player].score; }
EMSCRIPTEN_KEEPALIVE int poly_income(int player) { return env_income(&E.game, player); }
EMSCRIPTEN_KEEPALIVE int poly_tribe(int player) { return E.game.players[player].tribe; }
EMSCRIPTEN_KEEPALIVE unsigned poly_techs(int player) { return E.game.players[player].techs; }
EMSCRIPTEN_KEEPALIVE int poly_tech_cost(int player, int tech) {
    const Player* p = &E.game.players[player];
    return tech_cost((Tech)tech, p->num_cities, tech_researched(p, TECH_PHILOSOPHY));
}

// units: JS iterates tiles; per-tile unit info packed on demand
EMSCRIPTEN_KEEPALIVE int poly_unit_at(int tile) { return E.game.unit_at[tile]; }
EMSCRIPTEN_KEEPALIVE int poly_unit_type(int ui) { return E.game.units[ui].type; }
EMSCRIPTEN_KEEPALIVE int poly_unit_owner(int ui) { return E.game.units[ui].owner; }
EMSCRIPTEN_KEEPALIVE int poly_unit_hp(int ui) { return E.game.units[ui].hp; }
EMSCRIPTEN_KEEPALIVE int poly_unit_max_hp(int ui) { return E.game.units[ui].max_hp; }
EMSCRIPTEN_KEEPALIVE int poly_unit_status(int ui) { return E.game.units[ui].status; }
EMSCRIPTEN_KEEPALIVE int poly_unit_veteran(int ui) { return E.game.units[ui].veteran ? 1 : 0; }

// cities
EMSCRIPTEN_KEEPALIVE int poly_city_owner(int ci) { return E.game.cities[ci].owner; }
EMSCRIPTEN_KEEPALIVE int poly_city_level(int ci) { return E.game.cities[ci].level; }
EMSCRIPTEN_KEEPALIVE int poly_city_pop(int ci) { return E.game.cities[ci].population; }
EMSCRIPTEN_KEEPALIVE int poly_city_pop_need(int ci) { return E.game.cities[ci].population_need; }
EMSCRIPTEN_KEEPALIVE int poly_city_walls(int ci) { return E.game.cities[ci].has_walls ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int poly_city_capital(int ci) { return E.game.cities[ci].is_capital ? 1 : 0; }
