// polytopia-rl — RL environment wrapper for PufferLib (and any other driver).
//
// Turns the engine's variable-length legal-action list into a single masked
// Discrete head via a 3-phase micro-decision machine (the ocean/chess pattern):
//   SELECT: pick an own actor tile, or an immediate tribe action
//   VERB:   pick what the selected actor does
//   TARGET: pick a tile (only for verbs that need one)
//
// Two agents per env (players 0/1). Every tick both agents receive obs and
// masks; only the active player's action is applied — the waiting player's
// mask allows only PASS (index A_END_TURN), whose value is ignored.
//
// This header is PufferLib-agnostic: the binding (or a test) provides the
// per-agent buffer pointers.
#ifndef POLYTOPIA_ENV_H
#define POLYTOPIA_ENV_H

// repo layout uses ../core; the PufferLib ocean/polytopia layout copies core/
// alongside this header
#if defined(__has_include)
#  if __has_include("../core/mapgen.h")
#    include "../core/mapgen.h"
#  else
#    include "core/mapgen.h"
#  endif
#else
#  include "../core/mapgen.h"
#endif

#define ENV_SIZE 11
#define ENV_TILES (ENV_SIZE * ENV_SIZE)
#define ENV_PLAYERS 2

// ---------------------------------------------------------------------------
// Action space: [0, ENV_TILES) = tiles; [ENV_TILES, ENV_TILES+VERB_N) = verbs.
// Verb slots are globally disjoint so a selected tile can expose the union of
// its unit's and its city's verbs.
// ---------------------------------------------------------------------------
#define V_END_TURN 0            // also PASS for the waiting agent
#define V_RESEARCH0 1           // 1..24: research tech (immediate, SELECT phase)
#define V_BUILD_ROAD 25         // -> TARGET
#define V_UNIT0 26              // 26..35: unit verbs, order below
#define V_BUILD0 36             // 36..54: build building b -> TARGET
#define V_SPAWN0 55             // 55..62: spawn unit type (immediate)
#define V_LEVELUP0 63           // 63..70: level-up bonus (immediate)
#define V_GATHER 71             // -> TARGET
#define V_CLEAR_FOREST 72       // -> TARGET
#define V_BURN_FOREST 73        // -> TARGET
#define V_GROW_FOREST 74        // -> TARGET
#define V_DESTROY 75            // -> TARGET
#define VERB_N 76

#define ACTION_N (ENV_TILES + VERB_N)

// unit verb order within V_UNIT0..V_UNIT0+9
static const int8_t UNIT_VERB_KIND[10] = {
    ACT_MOVE, ACT_ATTACK, ACT_CAPTURE, ACT_RECOVER, ACT_HEAL_OTHERS,
    ACT_CONVERT, ACT_MAKE_VETERAN, ACT_UPGRADE, ACT_DISBAND, ACT_EXAMINE
};
static inline int unit_verb_slot(int kind) {
    for (int i = 0; i < 10; i++) if (UNIT_VERB_KIND[i] == kind) return V_UNIT0 + i;
    return -1;
}
static inline bool verb_needs_target(int kind) {
    return kind == ACT_MOVE || kind == ACT_ATTACK || kind == ACT_CONVERT ||
           kind == ACT_BUILD || kind == ACT_GATHER || kind == ACT_CLEAR_FOREST ||
           kind == ACT_BURN_FOREST || kind == ACT_GROW_FOREST || kind == ACT_DESTROY ||
           kind == ACT_BUILD_ROAD;
}

// ---------------------------------------------------------------------------
// Observation: 10 tile planes + globals, uint8. From the OBSERVING player's
// perspective (1 = own, 2 = enemy).
// ---------------------------------------------------------------------------
#define OBS_PLANES 10
#define OBS_GLOBALS (14 + 2 * NUM_TECH)
// named POLY_OBS_SIZE so the binding can define the literal OBS_SIZE macro
// that PufferLib's build tooling scrapes from binding.c
#define POLY_OBS_SIZE (OBS_PLANES * ENV_TILES + OBS_GLOBALS)
#ifndef OBS_SIZE
#define OBS_SIZE POLY_OBS_SIZE
#endif

typedef enum { PH_SELECT = 0, PH_VERB = 1, PH_TARGET = 2 } EnvPhase;

// All-float log struct; vecenv.h sums fields across envs and divides by n.
typedef struct {
    float score;            // final scores summed (avg of both players)
    float episode_length;   // micro-steps per episode
    float ticks;            // game turns per episode
    float p0_winrate;       // domination wins by player 0
    float draw_rate;        // games ended by turn cap / stall (no capitals win)
    float n;                // episodes accumulated
} Log;

typedef struct {
    Log log;

    // --- PufferLib framework fields (assigned by vecenv.h) ---
    uint8_t* observations;        // base of this env's agent-major slice
    float* actions;
    float* rewards;
    float* terminals;
    unsigned char* action_mask;   // non-NULL when MY_ACTION_MASK is defined
    int num_agents;               // = ENV_PLAYERS
    int tag;                      // selfplay pool (MY_USES_TAGS)
    int boundary_reached;

    // per-agent slot pointers (perm-aware; wired by my_setup_perm in the
    // binding, or by default wiring in poly_env_reset for tests)
    uint8_t* obs_ptr[ENV_PLAYERS];
    float* action_ptr[ENV_PLAYERS];
    float* reward_ptr[ENV_PLAYERS];
    float* terminal_ptr[ENV_PLAYERS];
    unsigned char* mask_ptr[ENV_PLAYERS];

    PolyState game;

    // decision-machine state
    int8_t phase;
    bool sel_is_unit;        // else city (when phase >= VERB)
    int16_t sel_idx;         // unit or city index
    int16_t sel_tile;
    int8_t pend_kind;        // ActKind for TARGET phase
    int8_t pend_arg;

    // enumerated legal actions for the active player (refreshed every tick)
    PolyAction legal[POLY_MAX_ACTIONS];
    int n_legal;

    float shaping;           // score-delta shaping coefficient (0 = off)
    float draw_penalty;      // subtracted from BOTH players on a stall/cap draw
    int fog;                 // partial observability (fog of war)
    int32_t prev_score[ENV_PLAYERS];
    int32_t episode_steps;
    int32_t max_episode_steps;
    uint32_t rng;            // env-level rng (map seeds, tribe picks)
    int8_t final_result[ENV_PLAYERS];   // last finished episode (env auto-resets)
} PolyEnv;

// ---------------------------------------------------------------------------
// Legal actions -> masks
// ---------------------------------------------------------------------------
static inline void env_refresh_legal(PolyEnv* e) {
    e->n_legal = poly_enumerate_actions(&e->game, e->legal, POLY_MAX_ACTIONS);
}

static inline int env_actor_tile(const PolyEnv* e, const PolyAction* a) {
    const PolyState* s = &e->game;
    switch ((ActKind)a->kind) {
        case ACT_MOVE: case ACT_ATTACK: case ACT_CAPTURE: case ACT_RECOVER:
        case ACT_HEAL_OTHERS: case ACT_CONVERT: case ACT_MAKE_VETERAN:
        case ACT_UPGRADE: case ACT_DISBAND: case ACT_EXAMINE:
            return tile_idx(s, s->units[a->actor].x, s->units[a->actor].y);
        case ACT_BUILD: case ACT_SPAWN: case ACT_LEVELUP: case ACT_GATHER:
        case ACT_CLEAR_FOREST: case ACT_BURN_FOREST: case ACT_GROW_FOREST:
        case ACT_DESTROY:
            return tile_idx(s, s->cities[a->actor].x, s->cities[a->actor].y);
        default:
            return -1;   // tribe actions have no actor tile
    }
}

static inline int env_verb_slot(const PolyAction* a) {
    switch ((ActKind)a->kind) {
        case ACT_END_TURN: return V_END_TURN;
        case ACT_RESEARCH: return V_RESEARCH0 + a->arg;
        case ACT_BUILD_ROAD: return V_BUILD_ROAD;
        case ACT_BUILD: return V_BUILD0 + a->arg;
        case ACT_SPAWN: return V_SPAWN0 + a->arg;
        case ACT_LEVELUP: return V_LEVELUP0 + a->arg;
        case ACT_GATHER: return V_GATHER;
        case ACT_CLEAR_FOREST: return V_CLEAR_FOREST;
        case ACT_BURN_FOREST: return V_BURN_FOREST;
        case ACT_GROW_FOREST: return V_GROW_FOREST;
        case ACT_DESTROY: return V_DESTROY;
        default: return unit_verb_slot(a->kind);
    }
}

// does `a` belong to the currently selected actor?
static inline bool env_action_of_selected(const PolyEnv* e, const PolyAction* a) {
    switch ((ActKind)a->kind) {
        case ACT_MOVE: case ACT_ATTACK: case ACT_CAPTURE: case ACT_RECOVER:
        case ACT_HEAL_OTHERS: case ACT_CONVERT: case ACT_MAKE_VETERAN:
        case ACT_UPGRADE: case ACT_DISBAND: case ACT_EXAMINE:
            return e->sel_is_unit && a->actor == e->sel_idx;
        case ACT_BUILD: case ACT_SPAWN: case ACT_LEVELUP: case ACT_GATHER:
        case ACT_CLEAR_FOREST: case ACT_BURN_FOREST: case ACT_GROW_FOREST:
        case ACT_DESTROY:
            return !e->sel_is_unit && a->actor == e->sel_idx;
        default:
            return false;
    }
}

// TARGET-phase tile for an action (attack/convert use the target unit's tile)
static inline int env_target_tile(const PolyEnv* e, const PolyAction* a) {
    if (a->kind == ACT_ATTACK || a->kind == ACT_CONVERT) {
        const Unit* t = &e->game.units[a->target];
        return tile_idx(&e->game, t->x, t->y);
    }
    return a->target;
}

static inline void env_write_mask(PolyEnv* e, int agent) {
    unsigned char* m = e->mask_ptr[agent];
    memset(m, 0, ACTION_N);
    if (e->game.game_over) { m[ENV_TILES + V_END_TURN] = 1; return; }
    if (agent != e->game.active_player) {           // waiting player: PASS only
        m[ENV_TILES + V_END_TURN] = 1;
        return;
    }
    switch ((EnvPhase)e->phase) {
        case PH_SELECT:
            for (int i = 0; i < e->n_legal; i++) {
                const PolyAction* a = &e->legal[i];
                int t = env_actor_tile(e, a);
                if (t >= 0) m[t] = 1;
                else m[ENV_TILES + env_verb_slot(a)] = 1;   // tribe actions
            }
            break;
        case PH_VERB:
            for (int i = 0; i < e->n_legal; i++)
                if (env_action_of_selected(e, &e->legal[i]))
                    m[ENV_TILES + env_verb_slot(&e->legal[i])] = 1;
            break;
        case PH_TARGET:
            for (int i = 0; i < e->n_legal; i++) {
                const PolyAction* a = &e->legal[i];
                if (a->kind != e->pend_kind) continue;
                if (a->kind == ACT_BUILD_ROAD) { m[a->target] = 1; continue; }
                if (!env_action_of_selected(e, a)) continue;
                if (a->kind == ACT_BUILD && a->arg != e->pend_arg) continue;
                m[env_target_tile(e, a)] = 1;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Observations
// ---------------------------------------------------------------------------
static inline void env_write_obs(PolyEnv* e, int agent) {
    const PolyState* s = &e->game;
    uint8_t* o = e->obs_ptr[agent];
    memset(o, 0, OBS_SIZE);
    uint8_t* terr = o;
    uint8_t* res = o + ENV_TILES;
    uint8_t* bld = o + 2 * ENV_TILES;
    uint8_t* road = o + 3 * ENV_TILES;
    uint8_t* city = o + 4 * ENV_TILES;
    uint8_t* utype = o + 5 * ENV_TILES;
    uint8_t* uown = o + 6 * ENV_TILES;
    uint8_t* uhp = o + 7 * ENV_TILES;
    uint8_t* ustat = o + 8 * ENV_TILES;
    uint8_t* sel = o + 9 * ENV_TILES;

    const Player* viewer = &s->players[agent];
    for (int t = 0; t < ENV_TILES; t++) {
        if (s->fog && !obs_get(viewer, t)) {   // unseen: fog terrain, nothing else
            terr[t] = TERRAIN_FOG;
            continue;
        }
        terr[t] = (uint8_t)s->terrain[t];
        res[t] = (uint8_t)s->resource[t];
        bld[t] = s->building[t] == BUILDING_NONE ? 19 : (uint8_t)s->building[t];
        road[t] = s->net_tile[t];
        int ci = s->city_at[t];
        if (ci >= 0) {
            const City* c = &s->cities[ci];
            bool own = c->owner == agent;
            bool center = tile_idx(s, c->x, c->y) == t;
            int lvl = c->level > 9 ? 9 : c->level;
            city[t] = (uint8_t)(center ? (own ? 10 + lvl : 20 + lvl) : (own ? 1 : 2));
        }
        int16_t ui = s->unit_at[t];
        if (ui >= 0) {
            const Unit* u = &s->units[ui];
            utype[t] = (uint8_t)(u->type + 1);
            uown[t] = u->owner == agent ? 1 : 2;
            uhp[t] = (uint8_t)(u->hp > 255 ? 255 : u->hp);
            ustat[t] = (uint8_t)(u->status + 1);
        }
    }
    if (e->phase != PH_SELECT && e->sel_tile >= 0) sel[e->sel_tile] = 1;

    uint8_t* g = o + OBS_PLANES * ENV_TILES;
    int opp = 1 - agent;
    const Player* me = &s->players[agent];
    const Player* op = &s->players[opp];
    int gi = 0;
    g[gi++] = (uint8_t)e->phase;
    g[gi++] = (uint8_t)(e->phase == PH_TARGET ? 1 + e->pend_kind : 0);
    g[gi++] = agent == s->active_player ? 1 : 0;
    g[gi++] = (uint8_t)(me->stars > 200 ? 200 : me->stars);
    g[gi++] = s->fog ? 0 : (uint8_t)(op->stars > 200 ? 200 : op->stars);  // fog hides enemy economy
    g[gi++] = (uint8_t)(me->score / 50 > 255 ? 255 : me->score / 50);
    g[gi++] = (uint8_t)(op->score / 50 > 255 ? 255 : op->score / 50);
    g[gi++] = (uint8_t)s->tick;
    g[gi++] = (uint8_t)me->num_cities;
    g[gi++] = (uint8_t)op->num_cities;
    g[gi++] = (uint8_t)me->num_kills;
    g[gi++] = (uint8_t)op->num_kills;
    g[gi++] = (uint8_t)me->tribe;
    g[gi++] = (uint8_t)op->tribe;
    for (int t = 0; t < NUM_TECH; t++) g[gi++] = (me->techs >> t) & 1u;
    for (int t = 0; t < NUM_TECH; t++) g[gi++] = s->fog ? 0 : (op->techs >> t) & 1u;
}

// ---------------------------------------------------------------------------
// Reset / step
// ---------------------------------------------------------------------------
static inline void env_new_game(PolyEnv* e) {
    int8_t tribes[2];
    tribes[0] = (int8_t)poly_rand_int(&e->rng, NUM_TRIBE);
    tribes[1] = (int8_t)poly_rand_int(&e->rng, NUM_TRIBE);
    uint32_t seed = poly_rand(&e->rng) | 1u;
    poly_reset(&e->game, ENV_PLAYERS, tribes, ENV_SIZE, MODE_CAPITALS, seed, e->fog != 0);
    e->phase = PH_SELECT;
    e->sel_idx = -1; e->sel_tile = -1; e->sel_is_unit = false;
    e->pend_kind = -1; e->pend_arg = -1;
    e->episode_steps = 0;
    e->prev_score[0] = e->game.players[0].score;
    e->prev_score[1] = e->game.players[1].score;
    env_refresh_legal(e);
}

static inline void env_emit(PolyEnv* e) {
    for (int a = 0; a < ENV_PLAYERS; a++) {
        env_write_obs(e, a);
        env_write_mask(e, a);
    }
}

// default per-slot wiring from the framework base pointers (identity perm);
// a MY_USES_PERM binding overrides via my_setup_perm.
static inline void env_wire_default(PolyEnv* e) {
    for (int s = 0; s < ENV_PLAYERS; s++) {
        e->obs_ptr[s] = e->observations + (size_t)s * OBS_SIZE;
        e->action_ptr[s] = e->actions + s;
        e->reward_ptr[s] = e->rewards + s;
        e->terminal_ptr[s] = e->terminals + s;
        e->mask_ptr[s] = e->action_mask + (size_t)s * ACTION_N;
    }
}

static inline void poly_env_reset(PolyEnv* e) {
    if (!e->rng) e->rng = 0x9e3779b9u;
    if (!e->max_episode_steps) e->max_episode_steps = 8192;
    if (e->obs_ptr[0] == NULL) env_wire_default(e);
    env_new_game(e);
    env_emit(e);
}

// find + execute the unique legal action matching the pending micro-decision;
// returns false if none (masked actions should make this impossible)
static inline bool env_exec(PolyEnv* e, const PolyAction* proto) {
    for (int i = 0; i < e->n_legal; i++) {
        const PolyAction* a = &e->legal[i];
        if (a->kind != proto->kind) continue;
        if (proto->actor >= 0 && a->actor != proto->actor) continue;
        if (proto->arg >= 0 && a->arg != proto->arg) continue;
        if (proto->target >= 0) {
            int tt = a->kind == ACT_ATTACK || a->kind == ACT_CONVERT
                     ? env_target_tile(e, a) : a->target;
            if (tt != proto->target) continue;
        }
        return poly_step(&e->game, a);
    }
    return false;
}

// Might rules: only capturing all capitals wins (+1/-1). A turn-cap or
// stall end is a DRAW — reward 0, final_result INCOMPLETE for both.
static inline void env_end_episode(PolyEnv* e) {
    bool domination = e->game.won_by_domination;
    for (int a = 0; a < ENV_PLAYERS; a++) {
        if (domination) {
            *e->reward_ptr[a] += e->game.players[a].result == RESULT_WIN ? 1.0f : -1.0f;
            e->final_result[a] = (int8_t)e->game.players[a].result;
        } else {
            *e->reward_ptr[a] -= e->draw_penalty;     // stalling hurts both sides
            e->final_result[a] = RESULT_INCOMPLETE;   // draw
        }
        *e->terminal_ptr[a] = 1.0f;
    }
    e->boundary_reached = 1;   // selfplay pool episode boundary
    e->log.score += (float)(e->game.players[0].score + e->game.players[1].score) / 2.0f;
    e->log.episode_length += (float)e->episode_steps;
    e->log.ticks += (float)e->game.tick;
    e->log.p0_winrate += domination && e->game.players[0].result == RESULT_WIN ? 1.0f : 0.0f;
    e->log.draw_rate += domination ? 0.0f : 1.0f;
    e->log.n += 1.0f;
    env_new_game(e);
}

// One tick: applies the active player's micro-action, updates rewards,
// terminals, obs and masks for both agents.
static inline void poly_env_step(PolyEnv* e) {
    *e->reward_ptr[0] = 0.0f; *e->reward_ptr[1] = 0.0f;
    *e->terminal_ptr[0] = 0.0f; *e->terminal_ptr[1] = 0.0f;

    int player = e->game.active_player;
    int act = (int)*e->action_ptr[player];
    bool ok = true;

    if (act < 0 || act >= ACTION_N) act = ENV_TILES + V_END_TURN;

    switch ((EnvPhase)e->phase) {
        case PH_SELECT:
            if (act >= ENV_TILES) {
                int v = act - ENV_TILES;
                if (v == V_END_TURN) {
                    ok = env_exec(e, &(PolyAction){ACT_END_TURN, -1, -1, -1});
                } else if (v >= V_RESEARCH0 && v < V_RESEARCH0 + NUM_TECH) {
                    ok = env_exec(e, &(PolyAction){ACT_RESEARCH, -1, -1, (int8_t)(v - V_RESEARCH0)});
                } else if (v == V_BUILD_ROAD) {
                    e->pend_kind = ACT_BUILD_ROAD; e->pend_arg = -1;
                    e->sel_idx = -1; e->sel_tile = -1; e->sel_is_unit = false;
                    e->phase = PH_TARGET;
                } else ok = false;
            } else {
                // tile: prefer the unit if it has legal actions, else the city
                int16_t ui = e->game.unit_at[act];
                bool found = false;
                if (ui >= 0) {
                    for (int i = 0; i < e->n_legal && !found; i++) {
                        int t = env_actor_tile(e, &e->legal[i]);
                        if (t == act && e->legal[i].kind >= ACT_MOVE && e->legal[i].kind <= ACT_EXAMINE &&
                            e->legal[i].actor == ui) {
                            e->sel_is_unit = true; e->sel_idx = ui; found = true;
                        }
                    }
                }
                if (!found) {
                    int ci = e->game.city_at[act];
                    if (ci >= 0) {
                        const City* c = &e->game.cities[ci];
                        if (tile_idx(&e->game, c->x, c->y) == act) {
                            for (int i = 0; i < e->n_legal && !found; i++) {
                                if (env_actor_tile(e, &e->legal[i]) == act &&
                                    e->legal[i].kind >= ACT_BUILD && e->legal[i].actor == ci) {
                                    e->sel_is_unit = false; e->sel_idx = (int16_t)ci; found = true;
                                }
                            }
                        }
                    }
                }
                if (found) { e->sel_tile = (int16_t)act; e->phase = PH_VERB; }
                else ok = false;
            }
            break;

        case PH_VERB: {
            if (act < ENV_TILES) { ok = false; break; }
            int v = act - ENV_TILES;
            int kind = -1, arg = -1;
            if (v >= V_UNIT0 && v < V_UNIT0 + 10) kind = UNIT_VERB_KIND[v - V_UNIT0];
            else if (v >= V_BUILD0 && v < V_BUILD0 + NUM_BUILDING) { kind = ACT_BUILD; arg = v - V_BUILD0; }
            else if (v >= V_SPAWN0 && v < V_SPAWN0 + 8) { kind = ACT_SPAWN; arg = v - V_SPAWN0; }
            else if (v >= V_LEVELUP0 && v < V_LEVELUP0 + NUM_LEVELUP) { kind = ACT_LEVELUP; arg = v - V_LEVELUP0; }
            else if (v == V_GATHER) kind = ACT_GATHER;
            else if (v == V_CLEAR_FOREST) kind = ACT_CLEAR_FOREST;
            else if (v == V_BURN_FOREST) kind = ACT_BURN_FOREST;
            else if (v == V_GROW_FOREST) kind = ACT_GROW_FOREST;
            else if (v == V_DESTROY) kind = ACT_DESTROY;
            if (kind < 0) { ok = false; break; }
            if (verb_needs_target(kind)) {
                e->pend_kind = (int8_t)kind; e->pend_arg = (int8_t)arg;
                e->phase = PH_TARGET;
            } else {
                ok = env_exec(e, &(PolyAction){(int8_t)kind, e->sel_idx, -1, (int8_t)arg});
                e->phase = PH_SELECT;
                e->sel_idx = -1; e->sel_tile = -1;
            }
            break;
        }

        case PH_TARGET:
            if (act >= ENV_TILES) { ok = false; break; }
            ok = env_exec(e, &(PolyAction){e->pend_kind,
                                           e->pend_kind == ACT_BUILD_ROAD ? -1 : e->sel_idx,
                                           (int16_t)act, e->pend_arg});
            e->phase = PH_SELECT;
            e->sel_idx = -1; e->sel_tile = -1;
            e->pend_kind = -1; e->pend_arg = -1;
            break;
    }

    if (!ok) {   // illegal (shouldn't happen with masks): forced end turn
        env_exec(e, &(PolyAction){ACT_END_TURN, -1, -1, -1});
        e->phase = PH_SELECT;
        e->sel_idx = -1; e->sel_tile = -1;
        e->pend_kind = -1; e->pend_arg = -1;
    }

    e->episode_steps++;

    // score-delta shaping (zero-sum)
    if (e->shaping != 0.0f && !e->game.game_over) {
        for (int a = 0; a < ENV_PLAYERS; a++) {
            int32_t d = e->game.players[a].score - e->prev_score[a];
            *e->reward_ptr[a] += e->shaping * (float)d;
            *e->reward_ptr[1 - a] -= e->shaping * (float)d;
            e->prev_score[a] = e->game.players[a].score;
        }
    }

    if (e->game.game_over) {
        env_end_episode(e);
    } else if (e->episode_steps >= e->max_episode_steps) {
        // stall guard: force end-of-game ranking by current score
        e->game.tick = (int16_t)(e->game.max_turns + 1);
        poly_check_game_over(&e->game);
        env_end_episode(e);
    }

    env_refresh_legal(e);
    env_emit(e);
}

// ---------------------------------------------------------------------------
// PufferLib c_* entry points (single-TU: the binding or a test includes this
// header exactly once)
// ---------------------------------------------------------------------------
void c_reset(PolyEnv* e) { poly_env_reset(e); }
void c_step(PolyEnv* e) { poly_env_step(e); }
void c_render(PolyEnv* e) { (void)e; }   // headless; web/raylib viz lives elsewhere
void c_close(PolyEnv* e) { (void)e; }

#endif // POLYTOPIA_ENV_H
