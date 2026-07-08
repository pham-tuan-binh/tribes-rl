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
// ENV_PLAYERS is the number of agent SEATS (buffer sizing only). The actual
// player count is rolled PER EPISODE in [min_players, max_players]; unused
// seats are dummies that pass. Obs are player-agnostic (pooled opponents),
// so one model serves every player count.
#ifndef ENV_PLAYERS
#define ENV_PLAYERS 4
#endif

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
// perspective (1 = own, 2 = enemy). Player-agnostic: the tile planes already
// encode only mine/enemy, and the globals carry the SELF block plus a pooled
// summary of all living opponents — the same fixed layout for any player
// count (and any future count beyond 4).
// ---------------------------------------------------------------------------
#define OBS_PLANES 10
#define OBS_SELF_BLOCK (5 + NUM_TECH)
// pooled opponents: alive, seats-until-my-turn, sum/max cities, max score,
// sum kills, max stars (fog: 0), max tech count (fog: 0)
#define OBS_OPP_BLOCK 8
#define OBS_GLOBALS (4 + OBS_SELF_BLOCK + OBS_OPP_BLOCK)
// named POLY_OBS_SIZE so the binding can define the literal OBS_SIZE macro
// that PufferLib's build tooling scrapes from binding.c
#define POLY_OBS_SIZE (OBS_PLANES * ENV_TILES + OBS_GLOBALS)
#ifndef OBS_SIZE
#define OBS_SIZE POLY_OBS_SIZE
#endif

typedef enum { PH_SELECT = 0, PH_VERB = 1, PH_TARGET = 2 } EnvPhase;

#define POLY_MAX_BANKS 8

// All-float log struct; vecenv.h sums fields across envs and divides by n.
// slot_*_score and hist_* follow the chess contract read by pufferl.match and
// selfplay.py: primary outcome 1.0 win / 0.5 draw / 0.0 loss.
typedef struct {
    float score;            // final scores summed (avg of both players)
    float episode_length;   // micro-steps per episode
    float ticks;            // game turns per episode
    float p0_winrate;       // domination wins by player 0
    float draw_rate;        // games ended by turn cap / stall (no capitals win)
    float wins;             // domination-ended episodes
    float win_ticks;        // summed game turns of domination wins
                            // (avg turns-to-win = win_ticks / wins)
    float slot_0_score;     // outcome of agent slot 0 (primary in match/selfplay)
    float slot_1_score;
    float hist_score;       // primary outcome sums on opponent-pool (tagged) envs
    float hist_n;
    float hist_score_bank[POLY_MAX_BANKS];
    float hist_n_bank[POLY_MAX_BANKS];
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
    // both can be selected at once: a unit standing on its own city center
    // exposes the union of unit and city verbs (verb slots are disjoint)
    int16_t sel_unit;        // unit index, -1 = none
    int16_t sel_city;        // city index, -1 = none
    int16_t sel_tile;
    int8_t pend_kind;        // ActKind for TARGET phase
    int8_t pend_arg;

    // enumerated legal actions for the active player (refreshed every tick)
    PolyAction legal[POLY_MAX_ACTIONS];
    int n_legal;

    // --- reward shaping (all small vs the ±1 terminal; see polytopia.ini) ---
    float shaping;           // legacy generic score-delta coefficient (0 = off:
                             // Tribes score rewards temples/parks — anti-domination)
    float draw_penalty;      // subtracted from BOTH players on a stall/cap draw
    float speed_bonus;       // terminal multiplier: ±(1 + sb*(1 - turns/max_turns));
                             // faster conquest pays more (mirrored on the loser)
    float turn_penalty;      // dense time pressure: every game turn costs ALL
                             // players this much — drives convergence to short games
    int32_t prev_tick;
    float reward_city;       // per city gained/lost (captures transfer value)
    float reward_kill;       // per star-cost of units killed (favorable trades)
    float reward_explore;    // per fog tile revealed
    float reward_prod;       // per point of income growth (real economy, not score)
    float reward_waste;      // per star burned in do-undo cycles (grow->clear forest
                             // etc.) — the "road-spam class" of null actions
    float reward_capital;    // per enemy capital captured / own capital lost —
                             // the key intermediate objective in multiplayer
    int fog;                 // partial observability (fog of war)
    int min_players;         // per-episode player count is rolled uniformly
    int max_players;         // in [min_players, max_players] (2..ENV_PLAYERS)
    int32_t prev_score[ENV_PLAYERS];
    int32_t prev_cities[ENV_PLAYERS];
    int32_t prev_killval[ENV_PLAYERS];
    int32_t prev_lostval[ENV_PLAYERS];
    int32_t prev_seen[ENV_PLAYERS];
    int32_t prev_prod[ENV_PLAYERS];
    int32_t prev_waste[ENV_PLAYERS];
    int32_t prev_caps_taken[ENV_PLAYERS];
    int32_t prev_caps_lost[ENV_PLAYERS];
    int32_t episode_steps;
    int32_t max_episode_steps;
    uint32_t rng;            // env-level rng (map seeds, tribe picks)
    // last finished episode (the env auto-resets, so finals are snapshotted
    // here for the website's summary card)
    int8_t final_result[ENV_PLAYERS];
    int32_t final_score[ENV_PLAYERS];
    int32_t final_kills[ENV_PLAYERS];
    int32_t final_cities[ENV_PLAYERS];
    int32_t final_caps[ENV_PLAYERS];
    int32_t final_tick;
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
            return e->sel_unit >= 0 && a->actor == e->sel_unit;
        case ACT_BUILD: case ACT_SPAWN: case ACT_LEVELUP: case ACT_GATHER:
        case ACT_CLEAR_FOREST: case ACT_BURN_FOREST: case ACT_GROW_FOREST:
        case ACT_DESTROY:
            return e->sel_city >= 0 && a->actor == e->sel_city;
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
    if (agent >= s->num_players) return;   // dummy seat: zero obs, PASS-only mask
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
    int gi = 0;
    g[gi++] = (uint8_t)e->phase;
    g[gi++] = (uint8_t)(e->phase == PH_TARGET ? 1 + e->pend_kind : 0);
    g[gi++] = agent == s->active_player ? 1 : 0;
    g[gi++] = (uint8_t)(s->tick > 255 ? 255 : s->tick);

    // SELF block
    const Player* me = &s->players[agent];
    g[gi++] = (uint8_t)(me->stars > 200 ? 200 : me->stars);
    g[gi++] = (uint8_t)(me->score / 50 > 255 ? 255 : me->score / 50);
    g[gi++] = (uint8_t)me->num_cities;
    g[gi++] = (uint8_t)me->num_kills;
    g[gi++] = (uint8_t)(me->result == RESULT_LOSS ? 200 : me->tribe);
    for (int t = 0; t < NUM_TECH; t++)
        g[gi++] = (me->techs >> t) & 1u;

    // pooled OPPONENT block: identity-free summary of everyone else alive.
    // Fog hides opponents' stars and tech, as before.
    int np = s->num_players;
    int alive = 0, sum_cities = 0, max_cities = 0, max_score = 0, sum_kills = 0;
    int max_stars = 0, max_tech = 0;
    for (int k = 1; k < np; k++) {
        const Player* p = &s->players[(agent + k) % np];
        if (p->result == RESULT_LOSS) continue;
        alive++;
        sum_cities += p->num_cities;
        if (p->num_cities > max_cities) max_cities = p->num_cities;
        int sc = p->score / 50;
        if (sc > max_score) max_score = sc;
        sum_kills += p->num_kills;
        if (!s->fog) {
            if (p->stars > max_stars) max_stars = p->stars;
            int tc = __builtin_popcount((unsigned)p->techs);
            if (tc > max_tech) max_tech = tc;
        }
    }
    g[gi++] = (uint8_t)alive;
    // turn-order signal pooling loses: seats before I act again (0 = my turn)
    g[gi++] = (uint8_t)((agent - s->active_player + np) % np);
    g[gi++] = (uint8_t)(sum_cities > 255 ? 255 : sum_cities);
    g[gi++] = (uint8_t)max_cities;
    g[gi++] = (uint8_t)(max_score > 255 ? 255 : max_score);
    g[gi++] = (uint8_t)(sum_kills > 255 ? 255 : sum_kills);
    g[gi++] = (uint8_t)(max_stars > 200 ? 200 : max_stars);
    g[gi++] = (uint8_t)max_tech;
}

// ---------------------------------------------------------------------------
// Reset / step
// ---------------------------------------------------------------------------
static inline int env_income(const PolyState* s, int player) {
    int prod = 0;
    const Player* p = &s->players[player];
    for (int k = 0; k < p->num_cities; k++)
        prod += city_production(&s->cities[p->city_list[k]]);
    return prod;
}

static inline void env_new_game(PolyEnv* e) {
    int np = e->min_players;
    if (e->max_players > e->min_players)
        np += (int)(poly_rand(&e->rng) % (uint32_t)(e->max_players - e->min_players + 1));
    int8_t tribes[ENV_PLAYERS];
    for (int p = 0; p < ENV_PLAYERS; p++)
        tribes[p] = (int8_t)poly_rand_int(&e->rng, NUM_TRIBE);
    uint32_t seed = poly_rand(&e->rng) | 1u;
    poly_reset(&e->game, np, tribes, ENV_SIZE, MODE_CAPITALS, seed, e->fog != 0);
    e->phase = PH_SELECT;
    e->sel_unit = -1; e->sel_city = -1; e->sel_tile = -1;
    e->pend_kind = -1; e->pend_arg = -1;
    e->episode_steps = 0;
    e->prev_tick = 0;
    for (int p = 0; p < ENV_PLAYERS; p++) {
        e->prev_score[p] = e->game.players[p].score;
        e->prev_cities[p] = e->game.players[p].num_cities;
        e->prev_killval[p] = e->game.players[p].kill_value;
        e->prev_lostval[p] = e->game.players[p].lost_value;
        e->prev_seen[p] = e->game.players[p].tiles_seen;
        e->prev_prod[p] = env_income(&e->game, p);
        e->prev_waste[p] = e->game.players[p].stars_wasted;
        e->prev_caps_taken[p] = e->game.players[p].capitals_taken;
        e->prev_caps_lost[p] = e->game.players[p].capitals_lost;
    }
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
    if (e->min_players < 2) e->min_players = 2;
    if (e->max_players < e->min_players) e->max_players = ENV_PLAYERS;
    if (e->max_players > ENV_PLAYERS) e->max_players = ENV_PLAYERS;
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
    // speed multiplier: winning at turn 30/200 pays ~1.42x, at turn 190 ~1.02x
    float tick_frac = (float)(e->game.tick > e->game.max_turns ? e->game.max_turns : e->game.tick)
                      / (float)e->game.max_turns;
    float magnitude = 1.0f + e->speed_bonus * (1.0f - tick_frac);
    int np = e->game.num_players;
    e->final_tick = e->game.tick;
    for (int a = 0; a < ENV_PLAYERS; a++) {
        e->final_score[a] = e->game.players[a].score;
        e->final_kills[a] = e->game.players[a].num_kills;
        e->final_cities[a] = e->game.players[a].num_cities;
        e->final_caps[a] = e->game.players[a].capitals_taken;
        if (a >= np) {                                // dummy seat: no signal
            e->final_result[a] = RESULT_INCOMPLETE;
        } else if (domination) {
            *e->reward_ptr[a] += e->game.players[a].result == RESULT_WIN ? magnitude : -magnitude;
            e->final_result[a] = (int8_t)e->game.players[a].result;
        } else {
            *e->reward_ptr[a] -= e->draw_penalty;     // stalling hurts all sides
            e->final_result[a] = RESULT_INCOMPLETE;   // draw
        }
        *e->terminal_ptr[a] = 1.0f;
    }
    e->boundary_reached = 1;   // selfplay pool episode boundary

    // per-slot outcome for match/selfplay (1 win / 0.5 draw / 0 loss)
    float outcome0 = domination ? (e->game.players[0].result == RESULT_WIN ? 1.0f : 0.0f) : 0.5f;
    e->log.slot_0_score += outcome0;
    e->log.slot_1_score += 1.0f - outcome0;
    if (e->tag > 0 && e->tag <= POLY_MAX_BANKS) {   // opponent-pool env
        e->log.hist_score += outcome0;
        e->log.hist_n += 1.0f;
        e->log.hist_score_bank[e->tag - 1] += outcome0;
        e->log.hist_n_bank[e->tag - 1] += 1.0f;
    }

    e->log.score += (float)(e->game.players[0].score + e->game.players[1].score) / 2.0f;
    e->log.episode_length += (float)e->episode_steps;
    e->log.ticks += (float)e->game.tick;
    e->log.p0_winrate += domination && e->game.players[0].result == RESULT_WIN ? 1.0f : 0.0f;
    e->log.draw_rate += domination ? 0.0f : 1.0f;
    if (domination) {
        e->log.wins += 1.0f;
        e->log.win_ticks += (float)e->game.tick;
    }
    e->log.n += 1.0f;
    env_new_game(e);
}

// Abort a partially-entered micro-decision (UI cancel). Game state is
// untouched — only the SELECT/VERB/TARGET machine rewinds.
static inline void env_clear_selection(PolyEnv* e) {
    e->sel_unit = -1; e->sel_city = -1; e->sel_tile = -1;
    e->pend_kind = -1; e->pend_arg = -1;
}

static inline void poly_env_cancel(PolyEnv* e) {
    e->phase = PH_SELECT;
    env_clear_selection(e);
    env_refresh_legal(e);
    env_emit(e);
}

// One tick: applies the active player's micro-action, updates rewards,
// terminals, obs and masks for both agents.
static inline void poly_env_step(PolyEnv* e) {
    for (int a = 0; a < ENV_PLAYERS; a++) { *e->reward_ptr[a] = 0.0f; *e->terminal_ptr[a] = 0.0f; }

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
                    env_clear_selection(e);
                    e->pend_kind = ACT_BUILD_ROAD; e->pend_arg = -1;
                    e->phase = PH_TARGET;
                } else ok = false;
            } else {
                // tile: select the unit AND/OR the city on it — the verb mask
                // becomes the union of both actors' actions
                env_clear_selection(e);
                int16_t ui = e->game.unit_at[act];
                int ci = e->game.city_at[act];
                bool city_centered = ci >= 0 &&
                    tile_idx(&e->game, e->game.cities[ci].x, e->game.cities[ci].y) == act;
                for (int i = 0; i < e->n_legal; i++) {
                    if (env_actor_tile(e, &e->legal[i]) != act) continue;
                    const PolyAction* a = &e->legal[i];
                    if (ui >= 0 && a->kind >= ACT_MOVE && a->kind <= ACT_EXAMINE && a->actor == ui)
                        e->sel_unit = ui;
                    if (city_centered && a->kind >= ACT_BUILD && a->actor == ci)
                        e->sel_city = (int16_t)ci;
                }
                if (e->sel_unit >= 0 || e->sel_city >= 0) {
                    e->sel_tile = (int16_t)act;
                    e->phase = PH_VERB;
                } else ok = false;
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
            // dispatch to whichever selected actor owns this verb slot
            int16_t actor = kind >= ACT_MOVE && kind <= ACT_EXAMINE ? e->sel_unit : e->sel_city;
            if (verb_needs_target(kind)) {
                e->pend_kind = (int8_t)kind; e->pend_arg = (int8_t)arg;
                e->phase = PH_TARGET;
            } else {
                ok = env_exec(e, &(PolyAction){(int8_t)kind, actor, -1, (int8_t)arg});
                e->phase = PH_SELECT;
                env_clear_selection(e);
            }
            break;
        }

        case PH_TARGET: {
            if (act >= ENV_TILES) { ok = false; break; }
            int16_t actor = e->pend_kind == ACT_BUILD_ROAD ? -1
                          : e->pend_kind >= ACT_MOVE && e->pend_kind <= ACT_EXAMINE ? e->sel_unit
                          : e->sel_city;
            ok = env_exec(e, &(PolyAction){e->pend_kind, actor, (int16_t)act, e->pend_arg});
            e->phase = PH_SELECT;
            env_clear_selection(e);
            break;
        }
    }

    if (!ok) {   // illegal (shouldn't happen with masks): forced end turn
        env_exec(e, &(PolyAction){ACT_END_TURN, -1, -1, -1});
        e->phase = PH_SELECT;
        env_clear_selection(e);
    }

    e->episode_steps++;

    int np = e->game.num_players;

    // dense time pressure: each elapsed game turn costs every player
    if (!e->game.game_over && e->turn_penalty != 0.0f && e->game.tick > e->prev_tick) {
        float cost = e->turn_penalty * (float)(e->game.tick - e->prev_tick);
        for (int a = 0; a < np; a++) *e->reward_ptr[a] -= cost;
        e->prev_tick = e->game.tick;
    }

    // --- tactical reward shaping (domination-aligned; see field comments) ---
    if (!e->game.game_over) {
        for (int a = 0; a < np; a++) {
            const Player* p = &e->game.players[a];
            float r = 0.0f;

            // zero-sum transfers are split evenly across opponents
            const float opp_share = 1.0f / (float)(np - 1);

            int32_t d_city = p->num_cities - e->prev_cities[a];
            if (d_city != 0 && e->reward_city != 0.0f) {
                r += e->reward_city * (float)d_city;                    // gained/lost cities
                for (int o = 0; o < np; o++)
                    if (o != a) *e->reward_ptr[o] -= e->reward_city * (float)d_city * opp_share;
            }
            e->prev_cities[a] = p->num_cities;

            // trades: +value killed, -value lost (loss counted on the OWNER —
            // by any cause including disband, so retreating-by-disband is not
            // a way to deny the opponent their earned advantage)
            int32_t d_kill = p->kill_value - e->prev_killval[a];
            int32_t d_lost = p->lost_value - e->prev_lostval[a];
            r += e->reward_kill * (float)(d_kill - d_lost);
            e->prev_killval[a] = p->kill_value;
            e->prev_lostval[a] = p->lost_value;

            int32_t d_seen = p->tiles_seen - e->prev_seen[a];
            if (d_seen > 0) r += e->reward_explore * (float)d_seen;     // exploration
            e->prev_seen[a] = p->tiles_seen;

            int32_t inc = env_income(&e->game, a);
            int32_t d_prod = inc - e->prev_prod[a];
            if (d_prod != 0) r += e->reward_prod * (float)d_prod;       // real economy growth
            e->prev_prod[a] = inc;

            int32_t d_waste = p->stars_wasted - e->prev_waste[a];       // do-undo star burn
            if (d_waste > 0) r -= e->reward_waste * (float)d_waste;
            e->prev_waste[a] = p->stars_wasted;

            // capitals: the decisive intermediate objective (+taken, -lost)
            int32_t d_ct = p->capitals_taken - e->prev_caps_taken[a];
            int32_t d_cl = p->capitals_lost - e->prev_caps_lost[a];
            if (d_ct || d_cl) r += e->reward_capital * (float)(d_ct - d_cl);
            e->prev_caps_taken[a] = p->capitals_taken;
            e->prev_caps_lost[a] = p->capitals_lost;

            // legacy generic score shaping (default 0: score pays for
            // temples/parks, which is anti-domination)
            if (e->shaping != 0.0f) {
                int32_t d = p->score - e->prev_score[a];
                r += e->shaping * (float)d;
                for (int o = 0; o < np; o++)
                    if (o != a) *e->reward_ptr[o] -= e->shaping * (float)d * opp_share;
            }
            e->prev_score[a] = p->score;

            *e->reward_ptr[a] += r;
        }
    }

    if (e->game.game_over) {
        env_end_episode(e);
    } else if (e->episode_steps >= e->max_episode_steps) {
        // stall guard: end the episode as a draw (log the real tick first)
        int32_t real_tick = e->game.tick;
        e->game.tick = e->game.max_turns + 1;
        poly_check_game_over(&e->game);
        e->game.tick = real_tick;
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
