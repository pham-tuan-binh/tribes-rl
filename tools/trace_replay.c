// Golden-trace replayer: loads a Java-exported map CSV, replays the Java
// TraceDumper action stream, and byte-compares canonical legal-action sets and
// full state dumps at every step. Any divergence = port bug (or documented gap).
//
// Coordinates: Java positions are (x=row, y=col) and print as "(row,col)".
// The C engine stores x=col, y=row; all canonical output here prints (y,x).
//
// Usage: trace_replay <map.csv> <trace.jsonl>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/mapgen.h"

// ---------------------------------------------------------------------------
// Java enum name tables (declaration order = our enum values)
// ---------------------------------------------------------------------------
static const char* TERRAIN_NAME[] = {"PLAIN", "SHALLOW_WATER", "DEEP_WATER", "MOUNTAIN",
                                     "VILLAGE", "CITY", "FOREST", "FOG"};
static const char* RESOURCE_NAME[] = {"FISH", "FRUIT", "ANIMAL", "WHALES", "", "ORE", "CROPS", "RUINS"};
static const char* BUILDING_NAME[] = {"PORT", "MINE", "FORGE", "FARM", "WINDMILL", "CUSTOMS_HOUSE",
    "LUMBER_HUT", "SAWMILL", "TEMPLE", "WATER_TEMPLE", "FOREST_TEMPLE", "MOUNTAIN_TEMPLE",
    "ALTAR_OF_PEACE", "EMPERORS_TOMB", "EYE_OF_GOD", "GATE_OF_POWER", "GRAND_BAZAR",
    "PARK_OF_FORTUNE", "TOWER_OF_WISDOM"};
static const char* UNIT_NAME[] = {"WARRIOR", "RIDER", "DEFENDER", "SWORDMAN", "ARCHER", "CATAPULT",
    "KNIGHT", "MIND_BENDER", "BOAT", "SHIP", "BATTLESHIP", "SUPERUNIT"};
static const char* STATUS_NAME[] = {"FRESH", "MOVED", "ATTACKED", "MOVED_AND_ATTACKED", "PUSHED", "FINISHED"};
static const char* TECH_NAME[] = {"CLIMBING", "FISHING", "HUNTING", "ORGANIZATION", "RIDING",
    "ARCHERY", "FARMING", "FORESTRY", "FREE_SPIRIT", "MEDITATION", "MINING", "ROADS", "SAILING",
    "SHIELDS", "WHALING", "AQUATISM", "CHIVALRY", "CONSTRUCTION", "MATHEMATICS", "NAVIGATION",
    "SMITHERY", "SPIRITUALISM", "TRADE", "PHILOSOPHY"};
static const char* LEVELUP_NAME[] = {"WORKSHOP", "EXPLORER", "CITY_WALL", "RESOURCES",
    "POP_GROWTH", "BORDER_GROWTH", "PARK", "SUPERUNIT"};
static const char* RESULT_NAME[] = {"WIN", "LOSS", "INCOMPLETE"};

static PolyState S;
static int failures = 0;

// (row,col) — Java prints its (x,y); C row = y, col = x.
static int posstr(char* out, int x, int y) { return sprintf(out, "(%d,%d)", y, x); }

// ---------------------------------------------------------------------------
// Map CSV loader (LevelLoader port). Java line i, token j -> C x=j, y=i.
// ---------------------------------------------------------------------------
static char TERRAIN_CHAR_OF[128];
static char RESOURCE_CHAR_OF[128];

static void load_map(const char* path) {
    memset(TERRAIN_CHAR_OF, -1, sizeof(TERRAIN_CHAR_OF));
    memset(RESOURCE_CHAR_OF, -1, sizeof(RESOURCE_CHAR_OF));
    TERRAIN_CHAR_OF['.'] = TERRAIN_PLAIN; TERRAIN_CHAR_OF['s'] = TERRAIN_SHALLOW_WATER;
    TERRAIN_CHAR_OF['d'] = TERRAIN_DEEP_WATER; TERRAIN_CHAR_OF['m'] = TERRAIN_MOUNTAIN;
    TERRAIN_CHAR_OF['v'] = TERRAIN_VILLAGE; TERRAIN_CHAR_OF['c'] = TERRAIN_CITY;
    TERRAIN_CHAR_OF['f'] = TERRAIN_FOREST;
    RESOURCE_CHAR_OF['h'] = RESOURCE_FISH; RESOURCE_CHAR_OF['f'] = RESOURCE_FRUIT;
    RESOURCE_CHAR_OF['a'] = RESOURCE_ANIMAL; RESOURCE_CHAR_OF['w'] = RESOURCE_WHALES;
    RESOURCE_CHAR_OF['o'] = RESOURCE_ORE; RESOURCE_CHAR_OF['c'] = RESOURCE_CROPS;
    RESOURCE_CHAR_OF['r'] = RESOURCE_RUINS;

    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    char line[4096];
    char cap_tribe[MAX_PLAYERS];
    int cap_x[MAX_PLAYERS], cap_y[MAX_PLAYERS];
    int n_caps = 0;

    memset(&S, 0, sizeof(S));
    int row = 0, size = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == 0) continue;
        int col = 0;
        char* tok = strtok(line, ",\n");
        while (tok) {
            char terr = tok[0];
            char* colon = strchr(tok, ':');
            char res = colon && colon[1] && colon[1] != ' ' ? colon[1] : 0;
            if (size == 0) { /* infer later */ }
            int x = col, y = row;
            int t = y * MAX_MAP_SIZE + x;   // temporary indexing; re-pack after size known
            if (terr == 'c') {
                cap_tribe[n_caps] = (char)atoi(colon + 1);   // "resource" field = tribe key
                cap_x[n_caps] = x; cap_y[n_caps] = y;
                n_caps++;
                S.terrain[t] = TERRAIN_CITY;
                S.resource[t] = RESOURCE_NONE;
            } else {
                S.terrain[t] = TERRAIN_CHAR_OF[(int)terr];
                if (S.terrain[t] < 0) { fprintf(stderr, "bad terrain '%c'\n", terr); exit(2); }
                S.resource[t] = res ? RESOURCE_CHAR_OF[(int)res] : RESOURCE_NONE;
            }
            col++;
            tok = strtok(NULL, ",\n");
        }
        if (row == 0) size = col;
        row++;
    }
    fclose(f);
    if (row != size) { fprintf(stderr, "non-square map %dx%d\n", size, row); exit(2); }

    // re-pack planes from MAX_MAP_SIZE stride to size stride
    int8_t terr2[MAX_TILES], res2[MAX_TILES];
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        terr2[y * size + x] = S.terrain[y * MAX_MAP_SIZE + x];
        res2[y * size + x] = S.resource[y * MAX_MAP_SIZE + x];
    }

    memset(&S, 0, sizeof(S));
    S.size = (int8_t)size;
    S.num_players = (int8_t)n_caps;
    S.mode = MODE_CAPITALS;
    S.rng = 1;
    S.max_turns = MAX_TURNS_CAPITALS;
    for (int t = 0; t < size * size; t++) {
        S.terrain[t] = terr2[t]; S.resource[t] = res2[t];
        S.building[t] = BUILDING_NONE; S.unit_at[t] = -1; S.city_at[t] = -1;
    }

    for (int p = 0; p < n_caps; p++) {
        Player* pl = &S.players[p];
        pl->tribe = cap_tribe[p];
        pl->result = RESULT_INCOMPLETE;
        pl->stars = INITIAL_STARS;
        memset(pl->obs, 0xff, sizeof(pl->obs));
        int8_t tech = TRIBE_INFO[(int)cap_tribe[p]].initial_tech;
        if (tech != TECH_NONE) {
            pl->techs |= 1u << tech;
            pl->score = TECH_INFO[tech].tier * TECH_TIER_POINTS;
        }
        int ci = S.num_cities++;
        City* c = &S.cities[ci];
        c->x = (int8_t)cap_x[p]; c->y = (int8_t)cap_y[p];
        c->owner = (int8_t)p; c->is_capital = true;
        c->level = 1; c->population_need = 2; c->bound = 1;
        if (cap_tribe[p] == TRIBE_LUXIDOOR) {
            c->level = 3; c->population = 0; c->population_need = 4; c->has_walls = true;
        }
        pl->capital = (int8_t)ci;
        pl->city_list[pl->num_cities++] = (int8_t)ci;
        S.net_tile[tile_idx(&S, c->x, c->y)] = 1;
        pl->score += CITY_CENTRE_POINTS;
        int ut = TRIBE_INFO[(int)cap_tribe[p]].starting_unit;
        int ui = poly_new_unit(&S, (UnitType)ut, p, c->x, c->y);
        city_add_unit(&S, ci, ui);
        pl->score += UNIT_STATS[ut].points;
        poly_assign_city_tiles(&S, ci, c->bound);
    }
    S.tick = 0;
    S.active_player = 0;
}

// ---------------------------------------------------------------------------
// Canonical state JSON — byte-exact mirror of TraceDumper.stateJson
// ---------------------------------------------------------------------------
static char* state_json(char* b) {
    char* p = b;
    int size = S.size;
    p += sprintf(p, "{\"tick\":%d,\"tiles\":{", S.tick);
    bool first = true;
    // Java iterates x(row) outer, y(col) inner
    for (int row = 0; row < size; row++) for (int col = 0; col < size; col++) {
        int t = tile_idx(&S, col, row);
        int ci = S.city_at[t];
        char cpos[24] = "";
        if (ci >= 0) posstr(cpos, S.cities[ci].x, S.cities[ci].y);
        if (!first) *p++ = ',';
        first = false;
        p += sprintf(p, "\"(%d,%d)\":\"%s|%s|%s|%d|%s\"", row, col,
                     TERRAIN_NAME[(int)S.terrain[t]],
                     S.resource[t] == RESOURCE_NONE ? "" : RESOURCE_NAME[(int)S.resource[t]],
                     S.building[t] == BUILDING_NONE ? "" : BUILDING_NAME[(int)S.building[t]],
                     S.net_tile[t] ? 1 : 0, cpos);
    }
    p += sprintf(p, "},\"units\":{");
    first = true;
    for (int row = 0; row < size; row++) for (int col = 0; col < size; col++) {
        int16_t ui = S.unit_at[tile_idx(&S, col, row)];
        if (ui < 0) continue;
        Unit* u = &S.units[ui];
        char base[24] = "";
        if (UNIT_STATS[u->type].water)
            snprintf(base, sizeof(base), "%s", UNIT_NAME[u->carried >= 0 ? u->carried : UNIT_WARRIOR]);
        if (!first) *p++ = ',';
        first = false;
        p += sprintf(p, "\"(%d,%d)\":\"%s|%d|%d|%d|%d|%d|%s|%s\"", row, col,
                     UNIT_NAME[(int)u->type], u->owner, u->hp, u->max_hp, u->kills,
                     u->veteran ? 1 : 0, STATUS_NAME[(int)u->status], base);
    }
    p += sprintf(p, "},\"cities\":{");
    first = true;
    for (int pl = 0; pl < S.num_players; pl++) {
        for (int k = 0; k < S.players[pl].num_cities; k++) {
            City* c = &S.cities[S.players[pl].city_list[k]];
            int ci = S.players[pl].city_list[k];
            // temples of this city, sorted by canonical string
            char temples[512] = "";
            char entries[16][48]; int n_t = 0;
            for (int t = 0; t < size * size; t++) {
                if (S.city_at[t] != ci || S.building[t] == BUILDING_NONE) continue;
                if (!BUILDING_INFO[(int)S.building[t]].is_temple) continue;
                snprintf(entries[n_t], sizeof(entries[0]), "(%d,%d):%d:%d;",
                         t / size, t % size, S.temple_level[t], S.temple_turns[t]);
                n_t++;
            }
            for (int i = 0; i < n_t; i++) for (int j = i + 1; j < n_t; j++)
                if (strcmp(entries[j], entries[i]) < 0) {
                    char tmp[48]; strcpy(tmp, entries[i]); strcpy(entries[i], entries[j]); strcpy(entries[j], tmp);
                }
            for (int i = 0; i < n_t; i++) strcat(temples, entries[i]);

            if (!first) *p++ = ',';
            first = false;
            p += sprintf(p, "\"(%d,%d)\":\"%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%s\"",
                         c->y, c->x, c->owner, c->level, c->population, c->population_need,
                         city_production(c), c->has_walls ? 1 : 0, c->bound, c->points_worth,
                         c->is_capital ? 1 : 0, c->num_units, temples);
        }
    }
    p += sprintf(p, "},\"players\":[");
    for (int pl = 0; pl < S.num_players; pl++) {
        Player* pp = &S.players[pl];
        if (pl > 0) *p++ = ',';
        p += sprintf(p, "{\"stars\":%d,\"score\":%d,\"kills\":%d,\"numCities\":%d,\"techs\":[",
                     pp->stars, pp->score, pp->num_kills, pp->num_cities);
        // tech names sorted alphabetically (Java sorts)
        int order[NUM_TECH]; int n_tech = 0;
        for (int t = 0; t < NUM_TECH; t++) if (tech_researched(pp, (Tech)t)) order[n_tech++] = t;
        for (int i = 0; i < n_tech; i++) for (int j = i + 1; j < n_tech; j++)
            if (strcmp(TECH_NAME[order[j]], TECH_NAME[order[i]]) < 0) {
                int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
            }
        for (int i = 0; i < n_tech; i++)
            p += sprintf(p, "%s\"%s\"", i ? "," : "", TECH_NAME[order[i]]);
        p += sprintf(p, "],\"connected\":[");
        char conn[MAX_CITIES][24]; int n_conn = 0;
        for (int ci = 0; ci < S.num_cities; ci++)
            if ((pp->connected_cities >> ci) & 1ull)
                posstr(conn[n_conn++], S.cities[ci].x, S.cities[ci].y);
        for (int i = 0; i < n_conn; i++) for (int j = i + 1; j < n_conn; j++)
            if (strcmp(conn[j], conn[i]) < 0) {
                char tmp[24]; strcpy(tmp, conn[i]); strcpy(conn[i], conn[j]); strcpy(conn[j], tmp);
            }
        for (int i = 0; i < n_conn; i++)
            p += sprintf(p, "%s\"%s\"", i ? "," : "", conn[i]);
        p += sprintf(p, "],\"result\":\"%s\"}", RESULT_NAME[(int)pp->result]);
    }
    p += sprintf(p, "]}");
    return b;
}

// ---------------------------------------------------------------------------
// Canonical action strings — mirror of TraceDumper.canon
// ---------------------------------------------------------------------------
static void canon_action(const PolyAction* a, char* out) {
    char p1[24], p2[24];
    switch ((ActKind)a->kind) {
        case ACT_END_TURN: strcpy(out, "END_TURN"); return;
        case ACT_RESEARCH: sprintf(out, "RESEARCH %s", TECH_NAME[(int)a->arg]); return;
        case ACT_BUILD_ROAD:
            posstr(p1, a->target % S.size, a->target / S.size);
            sprintf(out, "BUILD_ROAD %s", p1); return;
        case ACT_MOVE:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            posstr(p2, a->target % S.size, a->target / S.size);
            sprintf(out, "MOVE %s->%s", p1, p2); return;
        case ACT_ATTACK:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            posstr(p2, S.units[a->target].x, S.units[a->target].y);
            sprintf(out, "ATTACK %s->%s", p1, p2); return;
        case ACT_CONVERT:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            posstr(p2, S.units[a->target].x, S.units[a->target].y);
            sprintf(out, "CONVERT %s->%s", p1, p2); return;
        case ACT_CAPTURE:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            sprintf(out, "CAPTURE %s", p1); return;
        case ACT_RECOVER:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            sprintf(out, "RECOVER %s", p1); return;
        case ACT_HEAL_OTHERS:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            sprintf(out, "HEAL_OTHERS %s", p1); return;
        case ACT_MAKE_VETERAN:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            sprintf(out, "MAKE_VETERAN %s", p1); return;
        case ACT_UPGRADE:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            sprintf(out, "UPGRADE %s %s", p1, UNIT_NAME[(int)S.units[a->actor].type]); return;
        case ACT_DISBAND:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            sprintf(out, "DISBAND %s", p1); return;
        case ACT_EXAMINE:
            posstr(p1, S.units[a->actor].x, S.units[a->actor].y);
            sprintf(out, "EXAMINE %s", p1); return;
        case ACT_BUILD:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            posstr(p2, a->target % S.size, a->target / S.size);
            sprintf(out, "BUILD %s %s %s", p1, BUILDING_NAME[(int)a->arg], p2); return;
        case ACT_SPAWN:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            sprintf(out, "SPAWN %s %s", p1, UNIT_NAME[(int)a->arg]); return;
        case ACT_LEVELUP:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            sprintf(out, "LEVELUP %s %s", LEVELUP_NAME[(int)a->arg], p1); return;
        case ACT_GATHER:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            posstr(p2, a->target % S.size, a->target / S.size);
            sprintf(out, "GATHER %s %s", p1, p2); return;
        case ACT_CLEAR_FOREST:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            posstr(p2, a->target % S.size, a->target / S.size);
            sprintf(out, "CLEAR_FOREST %s %s", p1, p2); return;
        case ACT_BURN_FOREST:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            posstr(p2, a->target % S.size, a->target / S.size);
            sprintf(out, "BURN_FOREST %s %s", p1, p2); return;
        case ACT_GROW_FOREST:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            posstr(p2, a->target % S.size, a->target / S.size);
            sprintf(out, "GROW_FOREST %s %s", p1, p2); return;
        case ACT_DESTROY:
            posstr(p1, S.cities[a->actor].x, S.cities[a->actor].y);
            posstr(p2, a->target % S.size, a->target / S.size);
            sprintf(out, "DESTROY %s %s", p1, p2); return;
        default: strcpy(out, "UNKNOWN"); return;
    }
}

// ---------------------------------------------------------------------------
// Diff helpers
// ---------------------------------------------------------------------------
static void report_diff(const char* what, long lineno, const char* expect, const char* got) {
    failures++;
    int i = 0;
    while (expect[i] && got[i] && expect[i] == got[i]) i++;
    int start = i > 60 ? i - 60 : 0;
    fprintf(stderr, "MISMATCH %s (trace line %ld) at offset %d:\n  java: ...%.140s\n  c:    ...%.140s\n",
            what, lineno, i, expect + start, got + start);
}

// extract a {...} or [...] JSON substring starting at p (assumes no strings
// containing braces — true for our canonical format)
static int json_span(const char* p) {
    char open = *p, close = open == '{' ? '}' : ']';
    int depth = 0, i = 0;
    do {
        if (p[i] == open) depth++;
        else if (p[i] == close) depth--;
        i++;
    } while (depth > 0 && p[i - 1]);
    return i;
}

static char state_buf[1 << 20];
static char legal_buf[1 << 20];
static char canon_buf[POLY_MAX_ACTIONS][80];

static void compare_state(const char* line, long lineno) {
    const char* key = strstr(line, "\"state\":");
    if (!key) key = strstr(line, "\"init\":");
    if (!key) return;
    const char* js = strchr(key + 7, '{');
    int span = json_span(js);
    state_json(state_buf);
    if ((int)strlen(state_buf) != span || strncmp(state_buf, js, (size_t)span) != 0) {
        static char expect[1 << 20];
        memcpy(expect, js, (size_t)span); expect[span] = 0;
        report_diff("state", lineno, expect, state_buf);
    }
}

// sorted, comma-joined, quoted legal-set string for the active player
static void legal_string(char* out) {
    static PolyAction acts[POLY_MAX_ACTIONS];
    int n = poly_enumerate_actions(&S, acts, POLY_MAX_ACTIONS);
    for (int i = 0; i < n; i++) canon_action(&acts[i], canon_buf[i]);
    // sort canon strings
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
        if (strcmp(canon_buf[j], canon_buf[i]) < 0) {
            char tmp[80]; strcpy(tmp, canon_buf[i]);
            strcpy(canon_buf[i], canon_buf[j]); strcpy(canon_buf[j], tmp);
        }
    char* p = out;
    *p++ = '[';
    for (int i = 0; i < n; i++)
        p += sprintf(p, "%s\"%s\"", i ? "," : "", canon_buf[i]);
    *p++ = ']'; *p = 0;
}

// find and execute the C action whose canonical form equals `chosen`
static bool execute_canon(const char* chosen) {
    static PolyAction acts[POLY_MAX_ACTIONS];
    int n = poly_enumerate_actions(&S, acts, POLY_MAX_ACTIONS);
    for (int i = 0; i < n; i++) {
        char c[80];
        canon_action(&acts[i], c);
        if (strcmp(c, chosen) == 0)
            return poly_execute_action(&S, &acts[i]);
    }
    return false;
}

// apply post-capture unit->city assignments from the trace:
// {"(cr,cc)":["(ur,uc)","(ur,uc)"],...} — per-city ORDERED unit lists.
static void apply_assign(const char* p) {
    for (int ci = 0; ci < S.num_cities; ci++) S.cities[ci].num_units = 0;
    for (int ui = 0; ui < S.num_units; ui++)
        if (S.units[ui].type != UNIT_NONE) S.units[ui].city = -1;

    p++;   // skip '{'
    while (*p && *p != '}') {
        int crow, ccol;
        if (sscanf(p, "\"(%d,%d)\"", &crow, &ccol) != 2) break;
        int ci = S.city_at[tile_idx(&S, ccol, crow)];
        p = strchr(p, '[');
        const char* end = strchr(p, ']');
        while (p < end) {
            const char* q = strchr(p + 1, '"');
            if (!q || q > end) break;
            int urow, ucol;
            sscanf(q, "\"(%d,%d)\"", &urow, &ucol);
            int16_t ui = S.unit_at[tile_idx(&S, ucol, urow)];
            if (ui >= 0 && ci >= 0 && S.cities[ci].num_units < CITY_MAX_UNITS) {
                S.cities[ci].unit_ids[S.cities[ci].num_units++] = ui;
                S.units[ui].city = (int8_t)ci;
            }
            p = strchr(q + 1, '"') + 1;   // past this entry's closing quote
        }
        p = end + 1;
        if (*p == ',') p++;
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s map.csv trace.jsonl\n", argv[0]); return 2; }
    load_map(argv[1]);

    FILE* f = fopen(argv[2], "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }

    static char line[1 << 20];
    long lineno = 0;
    long steps = 0;
    bool pending_end_turn = false;
    bool pending_advance = false;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        if (failures > 5) break;

        if (strncmp(line, "{\"init\":", 8) == 0) {
            compare_state(line, lineno);
        } else if (strncmp(line, "{\"initTurn\":", 12) == 0) {
            int player = atoi(line + 12);
            if (pending_advance) {
                poly_advance_turn(&S);   // Java's gameOver+rotation ran between dumps
                pending_advance = false;
            }
            if (S.active_player != player) {
                fprintf(stderr, "MISMATCH active player (line %ld): java %d, c %d\n",
                        lineno, player, S.active_player);
                failures++;
                break;
            }
            poly_init_turn(&S);
            compare_state(line, lineno);
        } else if (strncmp(line, "{\"step\":", 8) == 0) {
            steps++;
            // compare legal sets
            const char* lg = strstr(line, "\"legal\":");
            const char* ljs = strchr(lg + 8, '[');
            int lspan = json_span(ljs);
            legal_string(legal_buf);
            if ((int)strlen(legal_buf) != lspan || strncmp(legal_buf, ljs, (size_t)lspan) != 0) {
                static char expect[1 << 20];
                memcpy(expect, ljs, (size_t)lspan); expect[lspan] = 0;
                report_diff("legal", lineno, expect, legal_buf);
            }
            // chosen
            const char* ch = strstr(line, "\"chosen\":\"") + 10;
            char chosen[80];
            int cl = 0;
            while (ch[cl] != '"' && cl < 79) { chosen[cl] = ch[cl]; cl++; }
            chosen[cl] = 0;
            if (strcmp(chosen, "END_TURN") == 0) {
                pending_end_turn = true;   // Java runs gs.endTurn after this line
            } else {
                if (!execute_canon(chosen)) {
                    fprintf(stderr, "MISMATCH cannot execute chosen '%s' (line %ld)\n", chosen, lineno);
                    failures++;
                    break;
                }
                poly_check_monuments(&S, S.active_player);
                const char* as = strstr(line, "\"assign\":");
                if (as) apply_assign(strchr(as + 9, '{'));
                compare_state(line, lineno);
            }
        } else if (strncmp(line, "{\"endTurn\":", 11) == 0) {
            if (pending_end_turn) {
                poly_end_turn_only(&S);   // recover only; Java dumps before gameOver
                pending_end_turn = false;
                pending_advance = true;
                compare_state(line, lineno);
            }
            // else: maxSteps cut the turn short in Java; stop comparing
            else break;
        } else if (strncmp(line, "{\"final\":", 9) == 0) {
            if (pending_advance) {
                poly_advance_turn(&S);    // assigns final results
                pending_advance = false;
            }
            char expect[128] = "[";
            for (int p = 0; p < S.num_players; p++)
                sprintf(expect + strlen(expect), "%s\"%s\"", p ? "," : "", RESULT_NAME[(int)S.players[p].result]);
            strcat(expect, "]");
            const char* js = strchr(line + 9, '[');
            int span = json_span(js);
            if ((int)strlen(expect) != span || strncmp(expect, js, (size_t)span) != 0) {
                char got[128];
                memcpy(got, js, (size_t)span); got[span] = 0;
                report_diff("final", lineno, got, expect);
            }
        }
    }
    fclose(f);

    if (failures) {
        fprintf(stderr, "REPLAY FAILED: %d mismatches over %ld steps\n", failures, steps);
        return 1;
    }
    printf("replay OK: %ld steps bit-faithful\n", steps);
    return 0;
}
