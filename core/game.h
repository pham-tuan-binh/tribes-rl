// polytopia-rl — turn machine and win conditions.
// Port of GameState.initTurn/endTurn/gameOver + Game.tick turn rotation.
#ifndef POLY_GAME_H
#define POLY_GAME_H

#include "actions.h"

// Monument availability thresholds checked whenever stars/kills/level change
// (Java checks inside Tribe.addStars/addKill/etc; a post-action sweep is
// equivalent at action granularity).
static inline void poly_check_monuments(PolyState* s, int player) {
    Player* p = &s->players[player];
    if (p->stars >= EMPERORS_TOMB_STARS &&
        p->monuments[BUILDING_EMPERORS_TOMB - 12] == MONUMENT_UNAVAILABLE)
        p->monuments[BUILDING_EMPERORS_TOMB - 12] = MONUMENT_AVAILABLE;
    if (p->num_kills >= GATE_OF_POWER_KILLS &&
        p->monuments[BUILDING_GATE_OF_POWER - 12] == MONUMENT_UNAVAILABLE)
        p->monuments[BUILDING_GATE_OF_POWER - 12] = MONUMENT_AVAILABLE;
}

// GameState.initTurn: income, temples, unit refresh, pacifist counter.
static inline void poly_init_turn(PolyState* s) {
    int player = s->active_player;
    Player* p = &s->players[player];

    // star income: sum of city production; a city with an enemy unit on its
    // center produces nothing. Tick 0: stars = INITIAL_STARS.
    if (s->tick == 0) {
        p->stars = INITIAL_STARS;
    } else {
        int acum = 0;
        for (int k = 0; k < p->num_cities; k++) {
            City* c = &s->cities[p->city_list[k]];
            int16_t ui = s->unit_at[tile_idx(s, c->x, c->y)];
            if (ui >= 0 && s->units[ui].owner != player) continue;
            acum += city_production(c);
        }
        if (acum > 0) p->stars += acum;
    }

    // temples grow: after TEMPLE_TURNS_TO_SCORE turns they level (max 5),
    // scoring TEMPLE_POINTS[level-1] to the tribe and the city's pointsWorth.
    int ntiles = s->size * s->size;
    for (int t = 0; t < ntiles; t++) {
        int ci = s->city_at[t];
        if (ci < 0 || s->cities[ci].owner != player) continue;
        if (s->building[t] == BUILDING_NONE || !BUILDING_INFO[s->building[t]].is_temple) continue;
        if (s->temple_level[t] >= TEMPLE_MAX_LEVEL) continue;
        if (--s->temple_turns[t] == 0) {
            s->temple_level[t]++;
            s->temple_turns[t] = TEMPLE_TURNS_TO_SCORE;
            int pts = TEMPLE_POINTS[s->temple_level[t] - 1];
            p->score += pts;
            s->cities[ci].points_worth = (int16_t)(s->cities[ci].points_worth + pts);
        }
    }

    // unit refresh: FRESH for all, except last turn's PUSHED start as MOVED
    for (int ui = 0; ui < s->num_units; ui++) {
        Unit* u = &s->units[ui];
        if (u->type == UNIT_NONE || u->owner != player) continue;
        u->status = u->status == STATUS_PUSHED ? STATUS_MOVED : STATUS_FRESH;
    }

    // pacifist counter (ALTAR_OF_PEACE unlock; needs MEDITATION).
    // Java quirk kept: Tribe.addPacifistCount has NO already-built guard, so
    // every 5th consecutive pacifist turn re-arms the Altar — it can be built
    // multiple times (verified in golden traces).
    if (tech_researched(p, TECH_MEDITATION)) {
        p->pacifist_count++;
        if (p->pacifist_count == ALTAR_OF_PEACE_TURNS)
            p->monuments[BUILDING_ALTAR_OF_PEACE - 12] = MONUMENT_AVAILABLE;
    }
    poly_check_monuments(s, player);
}

// GameState.gameOver + Game end-of-turn logic.
static inline void poly_check_game_over(PolyState* s) {
    int alive = 0, last_alive = -1;
    for (int pl = 0; pl < s->num_players; pl++)
        if (s->players[pl].result != RESULT_LOSS) { alive++; last_alive = pl; }

    if (s->mode == MODE_CAPITALS) {
        // a player controlling ALL original capitals wins
        for (int pl = 0; pl < s->num_players; pl++) {
            if (s->players[pl].result == RESULT_LOSS) continue;
            bool all = true;
            for (int o = 0; o < s->num_players; o++)
                if (!player_controls_city(s, pl, s->players[o].capital)) { all = false; break; }
            if (all) {
                for (int o = 0; o < s->num_players; o++)
                    s->players[o].result = o == pl ? RESULT_WIN : RESULT_LOSS;
                s->game_over = true;
                s->won_by_domination = true;
                return;
            }
        }
    }
    // Java: the <=1-alive termination is evaluated only in SCORE mode
    // (GameState.gameOver); in CAPITALS the all-capitals check covers it.
    if (s->mode == MODE_SCORE && alive <= 1) {
        if (last_alive >= 0) s->players[last_alive].result = RESULT_WIN;
        s->game_over = true;
        return;
    }
    if (s->tick > s->max_turns) {
        // rank by score (tie-breaks: techs, cities — production/diplomacy
        // tie-breaks omitted in v1; exact ranking parity via golden traces)
        int best = -1;
        long best_key = -1;
        for (int pl = 0; pl < s->num_players; pl++) {
            if (s->players[pl].result == RESULT_LOSS) continue;
            int ntech = 0;
            for (uint32_t b = s->players[pl].techs; b; b >>= 1) ntech += (int)(b & 1);
            long key = (long)s->players[pl].score * 10000 + ntech * 100 + s->players[pl].num_cities;
            if (key > best_key) { best_key = key; best = pl; }
        }
        for (int pl = 0; pl < s->num_players; pl++)
            s->players[pl].result = pl == best ? RESULT_WIN : RESULT_LOSS;
        s->game_over = true;
    }
}

// GameState.endTurn: auto-recover fresh units. Split from the game-over check
// and rotation so the golden-trace replayer can compare states at Java's
// exact dump points (endTurn state is dumped BEFORE gameOver runs).
static inline void poly_end_turn_only(PolyState* s) {
    int player = s->active_player;
    for (int ui = 0; ui < s->num_units; ui++) {
        Unit* u = &s->units[ui];
        if (u->type == UNIT_NONE || u->owner != player) continue;
        if (u->status == STATUS_FRESH && feas_recover(s, ui)) exec_recover(s, ui);
    }
}

// Game.tick post-turn: game-over evaluation, then rotate to the next living
// player (tick increments on wraparound).
static inline void poly_advance_turn(PolyState* s) {
    poly_check_game_over(s);
    if (s->game_over) return;

    int player = s->active_player;
    int next = player;
    for (int i = 0; i < s->num_players; i++) {
        next = (next + 1) % s->num_players;
        if (s->players[next].result != RESULT_LOSS) break;
    }
    if (next <= player) s->tick++;              // wrapped around: new round
    s->active_player = (int8_t)next;
}

static inline void poly_end_turn(PolyState* s) {
    poly_end_turn_only(s);
    poly_advance_turn(s);
    if (s->game_over) return;
    poly_init_turn(s);
    if (s->tick > s->max_turns) poly_check_game_over(s);
}

// One atomic step of the game: apply an action for the active player.
// END_TURN triggers the turn rotation. Returns false if the action was illegal.
static inline bool poly_step(PolyState* s, const PolyAction* a) {
    if (s->game_over) return false;
    if (a->kind == ACT_END_TURN) {
        if (player_has_levelup_pending(s, s->active_player)) return false;
        poly_end_turn(s);
        return true;
    }
    bool ok = poly_execute_action(s, a);
    if (ok) poly_check_monuments(s, s->active_player);
    // Java evaluates game over only at turn boundaries (Game.tick after
    // processTurn) — a mid-turn capture does not end the game until EndTurn.
    return ok;
}

// Start a game on an already-built board: player 0 opens at tick 0.
// Product rules: effectively no turn cap — games run until conquest.
// Tribes-compat mode keeps the reference caps (golden traces).
#define PRODUCT_MAX_TURNS 50000   // effectively no cap: games run until conquest
static inline void poly_begin(PolyState* s) {
    s->tick = 0;
    s->active_player = 0;
    s->game_over = false;
    s->max_turns = s->tribes_compat
        ? (s->mode == MODE_CAPITALS ? MAX_TURNS_CAPITALS : MAX_TURNS_SCORE)
        : PRODUCT_MAX_TURNS;
    poly_init_turn(s);
}

#endif // POLY_GAME_H
