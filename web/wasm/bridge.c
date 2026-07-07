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
#include "../../puffer/polytopia_env.h"

static PolyEnv E;
static uint8_t obs_buf[ENV_PLAYERS][POLY_OBS_SIZE];
static float act_buf[ENV_PLAYERS];
static float rew_buf[ENV_PLAYERS];
static float term_buf[ENV_PLAYERS];
static unsigned char mask_buf[ENV_PLAYERS][ACTION_N];

EMSCRIPTEN_KEEPALIVE int poly_action_n(void) { return ACTION_N; }
EMSCRIPTEN_KEEPALIVE int poly_obs_size(void) { return POLY_OBS_SIZE; }
EMSCRIPTEN_KEEPALIVE int poly_map_size(void) { return ENV_SIZE; }
EMSCRIPTEN_KEEPALIVE int poly_num_players(void) { return E.game.num_players; }

EMSCRIPTEN_KEEPALIVE void poly_new_game(unsigned seed, int fog) {
    memset(&E, 0, sizeof(E));
    E.observations = &obs_buf[0][0];
    E.actions = act_buf;
    E.rewards = rew_buf;
    E.terminals = term_buf;
    E.action_mask = &mask_buf[0][0];
    E.num_agents = ENV_PLAYERS;
    E.fog = fog;
    E.max_episode_steps = 1 << 30;   // product rules: no practical stop
    E.rng = seed ? seed : 1;
    poly_env_reset(&E);
}

// per-player visibility for fog rendering (1 = tile explored)
EMSCRIPTEN_KEEPALIVE int poly_visible(int player, int tile) {
    return E.game.fog ? (obs_get(&E.game.players[player], tile) ? 1 : 0) : 1;
}
EMSCRIPTEN_KEEPALIVE int poly_fog_enabled(void) { return E.game.fog ? 1 : 0; }

// Apply one micro-action for the active player. Returns 1 while the game is
// running, 0 once it ended (the env auto-resets; call poly_new_game to control
// the next seed instead).
EMSCRIPTEN_KEEPALIVE int poly_act(int action) {
    int player = E.game.active_player;
    act_buf[player] = (float)action;
    act_buf[1 - player] = (float)(ENV_TILES + V_END_TURN);
    poly_env_step(&E);
    return term_buf[0] > 0.5f ? 0 : 1;
}

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
EMSCRIPTEN_KEEPALIVE int poly_stars(int player) { return E.game.players[player].stars; }
EMSCRIPTEN_KEEPALIVE int poly_score(int player) { return E.game.players[player].score; }
EMSCRIPTEN_KEEPALIVE int poly_tribe(int player) { return E.game.players[player].tribe; }
EMSCRIPTEN_KEEPALIVE unsigned poly_techs(int player) { return E.game.players[player].techs; }

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
