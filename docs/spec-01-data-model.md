# The Battle of Polytopia (GAIGResearch/Tribes) — Game Data Model Reference

All paths relative to `reference/Tribes/`. This document is a porting spec for the TS/web and C/PufferLib ports. Primary sources: `src/core/Types.java`, `src/core/TribesConfig.java`, `src/core/Constants.java`.

Note upfront: this is a *reimplementation* of Polytopia (not the real game), so some values/mechanics diverge from the mobile game — divergences are flagged in section 6.

---

## 0. Numeric sizes (the "how many" summary)

| Thing | Count | Source |
|---|---|---|
| Technologies | **24** (tiers 1–3: 5 tier-1, 10 tier-2, 9 tier-3; 10 *leaf* techs incl. SHIELDS) | `Types.TECHNOLOGY` |
| Tribes | **12** | `Types.TRIBE` |
| Unit types | **12** (keys 0–11) | `Types.UNIT` |
| Resource types | **7** (keys 0,1,2,3,**5,6,7** — key 4 skipped!) | `Types.RESOURCE` |
| Building types | **19** (keys 0–18); 4 temples + 7 monuments among them | `Types.BUILDING` |
| Terrain types | **8** (keys 0–7, incl. FOG) | `Types.TERRAIN` |
| Turn-status states | 6 | `Types.TURN_STATUS` |
| City level-up options | 8 (2 per level tier) | `Types.CITY_LEVEL_UP` |
| Examine bonuses | 5 | `Types.EXAMINE_BONUS` |
| Action types | 25 | `Types.ACTION` |
| Board sizes by #tribes | `{-1,11,14,16,18,20,22,24}` indexed by `nTribes-1` → 2 tribes=11×11 … 8 tribes=24×24 | `TribesConfig.DEFAULT_MAP_SIZE` |
| Max turns | SCORE mode 30, CAPITALS mode 50 | `Constants.MAX_TURNS`, `MAX_TURNS_CAPITALS` |

Board is always **square** (`size × size`).

---

## 1. Complete enumerations

### 1.1 TERRAIN (`Types.java:704`)
Grid values stored as the enum; map-file char in parens.

| key | name | char | notes |
|---|---|---|---|
| 0 | PLAIN | `.` | buildable land |
| 1 | SHALLOW_WATER | `s` | needs SAILING to enter; ports here |
| 2 | DEEP_WATER | `d` | needs NAVIGATION to enter |
| 3 | MOUNTAIN | `m` | needs CLIMBING to enter |
| 4 | VILLAGE | `v` | capturable → becomes CITY |
| 5 | CITY | `c` | city center tile |
| 6 | FOREST | `f` | |
| 7 | FOG | ` ` (space) | used for hidden tiles in partial-obs copies |

`isWater()` = SHALLOW or DEEP.

### 1.2 RESOURCE (`Types.java:157`)
Note the non-contiguous keys (4 is unused). Fields: key, cost (stars), bonus (pop or stars), required tech.

| key | name | char | cost | bonus | tech req | gather effect |
|---|---|---|---|---|---|---|
| 0 | FISH | `h` | 2 | +1 pop | FISHING | population |
| 1 | FRUIT | `f` | 2 | +1 pop | ORGANIZATION | population |
| 2 | ANIMAL | `a` | 2 | +1 pop | HUNTING | population |
| 3 | WHALES | `w` | 0 | +10 stars | WHALING | stars |
| 5 | ORE | `o` | 0 | 0 | MINING | (used by MINE) |
| 6 | CROPS | `c` | 0 | 0 | FARMING | (used by FARM) |
| 7 | RUINS | `r` | 0 | 0 | none | examined by unit |

Costs/bonuses from `TribesConfig` lines 180–187. Resource visibility is masked by tech in partial-obs (`Board.maskResource`): CROPS needs ORGANIZATION, ORE needs CLIMBING, WHALES needs FISHING.

### 1.3 TECHNOLOGY tree (`Types.java:24`)
`researched[]` boolean array indexed by **enum ordinal** (declaration order below). Parent = prerequisite.

- Tier 1 (parent null): CLIMBING, FISHING, HUNTING, ORGANIZATION, RIDING
- Tier 2: ARCHERY←HUNTING, FARMING←ORGANIZATION, FORESTRY←HUNTING, FREE_SPIRIT←RIDING, MEDITATION←CLIMBING, MINING←CLIMBING, ROADS←RIDING, SAILING←FISHING, SHIELDS←ORGANIZATION, WHALING←FISHING
- Tier 3: AQUATISM←WHALING, CHIVALRY←FREE_SPIRIT, CONSTRUCTION←FARMING, MATHEMATICS←FORESTRY, NAVIGATION←SAILING, SMITHERY←MINING, SPIRITUALISM←ARCHERY, TRADE←ROADS, PHILOSOPHY←MEDITATION

**Cost formula** (`getCost`): `TECH_BASE_COST(4) + tier * numCities`; if PHILOSOPHY researched, cost ×0.2 (`TECH_DISCOUNT_VALUE`, truncated to int). **Points**: `tier * 100` (`TECH_TIER_POINTS`).

Ordinal order matters for the boolean array — the 10 "leaf" techs (SHIELDS, AQUATISM, CHIVALRY, CONSTRUCTION, MATHEMATICS, NAVIGATION, SMITHERY, SPIRITUALISM, TRADE, PHILOSOPHY) trigger the "everything researched" check.

### 1.4 TRIBE (`Types.java:87`)
Fields: key, name, initial tech, starting unit, 3 colors.

| key | name | initial tech | starting unit |
|---|---|---|---|
| 0 | Xin-Xi | CLIMBING | WARRIOR |
| 1 | Imperius | ORGANIZATION | WARRIOR |
| 2 | Bardur | HUNTING | WARRIOR |
| 3 | Oumaji | RIDING | RIDER |
| 4 | Kickoo | FISHING | WARRIOR |
| 5 | Hoodrick | ARCHERY | ARCHER |
| 6 | Luxidoor | **null** (none) | WARRIOR |
| 7 | Vengir | SMITHERY | SWORDMAN |
| 8 | Zebasi | FARMING | WARRIOR |
| 9 | Ai-Mo | MEDITATION | WARRIOR |
| 10 | Quetzali | SHIELDS | DEFENDER |
| 11 | Yadakk | ROADS | WARRIOR |

Luxidoor special case (`LevelLoader`): starts with capital at **level 3** (two `levelUp()` calls), walls on, population set to 0 — compensating for having no free starting tech.

### 1.5 UNIT (`Types.java:491`) + stats (`TribesConfig.java:5–111`)
Stats stored per-instance in `Unit` (ATK/DEF/MOV mutable; RANGE/COST final; maxHP/currentHP mutable). MAX_HP for water units is `-1` at construction (set from carried land unit).

| key | unit | ATK | DEF | MOV | HP | RANGE | COST | pts | tech req | flags |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | WARRIOR | 2 | 2 | 1 | 10 | 1 | 2 | 10 | (tribe) | fortify, spawnable |
| 1 | RIDER | 2 | 1 | 2 | 10 | 1 | 3 | 15 | RIDING | fortify, "Escape" status rules |
| 2 | DEFENDER | 1 | 3 | 1 | 15 | 1 | 3 | 15 | SHIELDS | fortify, move-or-attack only |
| 3 | SWORDMAN | 3 | 3 | 1 | 15 | 1 | 5 | 25 | SMITHERY | fortify, dash |
| 4 | ARCHER | 2 | 1 | 1 | 10 | **2** | 3 | 15 | ARCHERY | fortify, ranged, dash |
| 5 | CATAPULT | 4 | 0 | 1 | 10 | **3** | 8 | 40 | MATHEMATICS | ranged, move-or-attack |
| 6 | KNIGHT | 4 | 1 | **3** | 15 | 1 | 8 | 40 | CHIVALRY | fortify, "Persist" (extra attacks on kill) |
| 7 | MIND_BENDER | 0 | 1 | 1 | 10 | 1 | 5 | 25 | PHILOSOPHY | cannot attack; heals (HEAL=4), converts |
| 8 | BOAT | 1 | 1 | 2 | (carried) | 2 | 0 | 0 | SAILING | water, ranged, not spawnable |
| 9 | SHIP | 2 | 2 | 3 | (carried) | 2 | 5 | 0 | SAILING | water, ranged, not spawnable |
| 10 | BATTLESHIP | 4 | 3 | 3 | (carried) | 2 | 15 | 0 | NAVIGATION | water, ranged, not spawnable |
| 11 | SUPERUNIT | 5 | 4 | 1 | 40 | 1 | 10 | 50 | none (city level-up) | not spawnable |

Helper predicates in `UNIT`: `spawnable()` (all except BOAT/SHIP/BATTLESHIP/SUPERUNIT), `isWaterUnit()`, `isRanged()` (BOAT/SHIP/BATTLESHIP/ARCHER/CATAPULT), `canFortify()` (WARRIOR/RIDER/ARCHER/DEFENDER/SWORDMAN/KNIGHT).

General unit constants (`TribesConfig`): ATTACK_MODIFIER 4.5, DEFENCE_BONUS ×1.5, DEFENCE_IN_WALLS ×4.0, VETERAN_KILLS 3, VETERAN_PLUS_HP +5, RECOVER_PLUS_HP +2, RECOVER_IN_BORDERS_PLUS_HP +2, explorer NUM_STEPS 15.

### 1.6 BUILDING (`Types.java:223`) + costs (`TribesConfig.java:125–177`)
Fields: key, cost, bonus, tech req, terrain-requirement set. Adjacency/resource constraints via methods.

| key | building | cost | bonus | tech | terrain | constraint |
|---|---|---|---|---|---|---|
| 0 | PORT | 10 | 2 | SAILING | SHALLOW_WATER | trade node; +2 pop |
| 1 | MINE | 5 | 2 | MINING | MOUNTAIN | needs ORE resource |
| 2 | FORGE | 5 | 2 | SMITHERY | PLAIN | adj MINE |
| 3 | FARM | 5 | 2 | FARMING | PLAIN | needs CROPS resource |
| 4 | WINDMILL | 5 | 1 | CONSTRUCTION | PLAIN | adj FARM |
| 5 | CUSTOMS_HOUSE | 5 | 2 | TRADE | PLAIN | adj PORT |
| 6 | LUMBER_HUT | 2 | 1 | FORESTRY | FOREST | |
| 7 | SAWMILL | 5 | 1 | MATHEMATICS | PLAIN | adj LUMBER_HUT |
| 8 | TEMPLE | 20 | 1 | FREE_SPIRIT | PLAIN | temple |
| 9 | WATER_TEMPLE | 20 | 1 | AQUATISM | SHALLOW/DEEP water | temple |
| 10 | FOREST_TEMPLE | 15 | 1 | SPIRITUALISM | FOREST | temple |
| 11 | MOUNTAIN_TEMPLE | 20 | 1 | MEDITATION | MOUNTAIN | temple |
| 12 | ALTAR_OF_PEACE | 0 | 3 | MEDITATION | SHALLOW/PLAIN | monument |
| 13 | EMPERORS_TOMB | 0 | 3 | TRADE | SHALLOW/PLAIN | monument |
| 14 | EYE_OF_GOD | 0 | 3 | NAVIGATION | SHALLOW/PLAIN | monument |
| 15 | GATE_OF_POWER | 0 | 3 | null | SHALLOW/PLAIN | monument |
| 16 | GRAND_BAZAR | 0 | 3 | ROADS | SHALLOW/PLAIN | monument |
| 17 | PARK_OF_FORTUNE | 0 | 3 | null | SHALLOW/PLAIN | monument |
| 18 | TOWER_OF_WISDOM | 0 | 3 | PHILOSOPHY | SHALLOW/PLAIN | monument |

`isBase()` = FARM|MINE|LUMBER_HUT. Matching (production-pair) buildings: PORT↔CUSTOMS_HOUSE, FARM↔WINDMILL, MINE↔FORGE, LUMBER_HUT↔SAWMILL. Base building's bonus is applied per adjacent matching building; non-base adds its own bonus per adjacent base (see `City.applyBonus`).

**Monument unlock triggers** (`Tribe.java`): EMPERORS_TOMB at ≥100 stars; GATE_OF_POWER at ≥10 kills; GRAND_BAZAR at ≥5 connected cities; ALTAR_OF_PEACE after 5 pacifist turns (needs MEDITATION); PARK_OF_FORTUNE when a city hits level 5; TOWER_OF_WISDOM when all techs researched; EYE_OF_GOD when whole board revealed (partial-obs only). Each monument: cost 0, +3 pop, **+400 score** (`MONUMENT_POINTS`).

Temples: TEMPLE_TURNS_TO_SCORE 3; `TEMPLE_POINTS = {100,50,50,50,150}` awarded as temple levels up (max level 5). See `Temple.java`.

### 1.7 CITY_LEVEL_UP (`Types.java:419`)
Options by current level (2 choices each): L1→{WORKSHOP, EXPLORER}; L2→{CITY_WALL, RESOURCES}; L3→{POP_GROWTH, BORDER_GROWTH}; L4+→{PARK, SUPERUNIT}.
Effects (`LevelUpCommand`): WORKSHOP +1 production; EXPLORER launches explorer (15 random steps clearing view); CITY_WALL sets walls; RESOURCES +5 stars; POP_GROWTH +3 pop; BORDER_GROWTH expand border +1 tile; PARK +250 score; SUPERUNIT spawns a SuperUnit (pushes occupant).
Level-up points: `50 - reachedLevel*5` where reachedLevel is the option's enum field (WORKSHOP/EXPLORER=2, CITY_WALL/RESOURCES=3, POP/BORDER=4, PARK/SUPERUNIT=5) → 40/35/30/25. (The `level==1 ? 100` branch in `getLevelUpPoints` is dead code — the field is never 1.)

### 1.8 TURN_STATUS (`Types.java:145`)
FRESH, MOVED, ATTACKED, MOVED_AND_ATTACKED, PUSHED, FINISHED. Per-unit-type transition rules encode Polytopia's Dash/Escape/Persist/move-or-attack abilities — full state machine in `Unit.canTransitionTo`/`transitionToStatus` (`units/Unit.java:96–188`). Knight's `addKill()` resets status to ATTACKED to allow chained attacks ("Persist").

### 1.9 Other enums
- **GAME_MODE**: CAPITALS(0) [win by holding all capitals, max 50 turns], SCORE(1) [max 30 turns, win by score/last-standing].
- **RESULT**: WIN(0), LOSS(1), INCOMPLETE(2).
- **EXAMINE_BONUS** (ruins): SUPERUNIT, RESEARCH, POP_GROWTH(+3), EXPLORER, RESOURCES(+10).
- **DIRECTIONS**: NONE, LEFT, RIGHT, UP, DOWN (4-connectivity for the cross ops; but neighborhoods use Chebyshev/8-conn — see below).
- **ACTION** (25): city {BUILD, BURN_FOREST, CLEAR_FOREST, DESTROY, GROW_FOREST, LEVEL_UP, RESOURCE_GATHERING, SPAWN}; tribe {BUILD_ROAD, END_TURN, RESEARCH_TECH, DECLARE_WAR, SEND_STARS}; unit {ATTACK, CAPTURE, CONVERT, DISBAND, EXAMINE, HEAL_OTHERS, MAKE_VETERAN, MOVE, RECOVER}; other {CLIMB_MOUNTAIN, UPGRADE_BOAT, UPGRADE_SHIP}. Each maps to an `ActionCommand`.

---

## 2. Per-actor state fields

All actors extend `Actor` (`src/core/actors/Actor.java`): `int actorId` (unique game-wide, -1 default), `int tribeId`, `Vector2d position`. Actor IDs are assigned sequentially by `Board.actorIDcounter`; tribes get IDs 0..n-1 (same as tribeId).

### 2.1 Tribe (`src/core/actors/Tribe.java`)
- `ArrayList<Integer> citiesID` — owned cities
- `int capitalID`
- `Types.TRIBE tribe` — type
- `TechnologyTree techTree`
- `int stars` — currency (INITIAL_STARS = **5**, note comment shows debug value 1000)
- `Types.RESULT winner` (default INCOMPLETE)
- `int score`
- `boolean[][] obsGrid` — **fog-of-war visibility per tile** (size×size), owned by the tribe
- `ArrayList<Integer> connectedCities` — cities trade-connected to capital
- `HashMap<BUILDING,MONUMENT_STATUS> monuments` — 7 monuments, each UNAVAILABLE/AVAILABLE/BUILT
- `ArrayList<Integer> tribesMet`
- `ArrayList<Integer> extraUnits` — units not owned by a city (converted/displaced), cityId=-1
- `int nKills`
- `int nPacifistCount` — turns since last attack (if MEDITATION)
- `int starsSent`, `boolean hasDeclaredWar`, `int nWarsDeclared`, `int nStarsSent` — diplomacy/turn tracking

Score accrues from: techs (tier×100), population (×5 POINTS_PER_POPULATION), city centre (+100), each border tile (+20 CITY_BORDER_POINTS), clearing fog (+5 per tile CLEAR_VIEW_POINTS), monuments (+400), temples (progressive), level-ups. `getReverseScore()` used by heuristics.

### 2.2 City (`src/core/actors/City.java`)
- `int level` (start 1)
- `int population` (can go negative down to `-level`)
- `int population_need` (start 2; on level-up becomes `level+1`)
- `boolean isCapital`
- `int production` (accumulated building production)
- `boolean hasWalls`
- `int bound` — border radius (start 1)
- `int pointsWorth` — score to be transferred/lost on capture
- `ArrayList<Integer> unitsID` — units housed (cap = `level+1`, see `canAddUnit`)
- `LinkedList<Building> buildings`

`getProduction()` = if pop≥0: `level + production + (capital?1:0)`; else returns population (negative penalty). `levelUp()`: level++, population -= population_need, population_need = level+1. Units-per-city capacity = level+1.

### 2.3 Building (`src/core/actors/Building.java`)
`Vector2d position`, `Types.BUILDING type`, `int cityId`. Not an Actor (no actorId).

### 2.4 Temple (`src/core/actors/Temple.java`) extends Building
`int level` (0→1 on creation), `int turnsToScore` (=3 on each level). `newTurn()` decrements; at 0 levels up (max 5) and returns `TEMPLE_POINTS[level-1]`.

### 2.5 Unit (`src/core/actors/units/Unit.java`) — abstract base
`int ATK, DEF, MOV` (mutable); `final int RANGE, COST`; `int maxHP, currentHP`; `int kills`; `boolean isVeteran`; `int cityId` (-1 if tribe-owned "extra"); `Types.TURN_STATUS status`. Constructor sets currentHP=maxHP, status=FINISHED.
Veteran (`MakeVeteranCommand`): at VETERAN_KILLS(3) kills eligible; sets veteran, maxHP +=5, heals to full.
Water units (Boat/Ship/Battleship) additionally carry `Types.UNIT baseLandUnit` — the land unit they transform back into on disembark. Embark/disembark create a *new* unit instance carrying over HP/kills/veteran (`Board.embark`/`disembark`). `hide()` (partial obs) zeros cityId and kills.

---

## 3. Board / GameState memory layout

### 3.1 Board (`src/core/game/Board.java`) — per-tile arrays, all `[size][size]`
- `Types.TERRAIN[][] terrains`
- `Types.RESOURCE[][] resources` (null = none)
- `Types.BUILDING[][] buildings` (null = none)
- `int[][] units` — **unit actorId per tile; 0 = empty** (note: 0 is the "no unit" sentinel, so no unit ever gets actorId 0 — tribes take 0..n-1 as actorIds, and actorIDcounter increments before first unit)
- `int[][] tileCityId` — owning city's actorId, -1 = unowned
- `Tribe[] tribes`
- `int[] capitalIDs` — capital city id per tribe index
- `HashMap<Integer,Actor> gameActors` — **central id→actor registry** (holds Cities and Units; Tribes are also given actorIds 0..n-1 but primarily live in `tribes[]`)
- `int size`, `int activeTribeID` (-1 default), `int actorIDcounter`
- `TradeNetwork tradeNetwork` — boolean grid of road/port/city trade tiles (see `src/core/game/TradeNetwork.java`)
- `Diplomacy diplomacy`
- `boolean isNative` — true for the real game state, false for agent copies

Fog of war is **not** on the Board — it's per-tribe `obsGrid`. `Board.copy(partialObs, playerId)` masks: non-visible tiles become FOG terrain, resources masked by tech, other tribes' actors hidden and their info stripped via `copy(hideInfo=true)`.

Key board ops: `getUnitAt`, `getCityInBorders`, `getBuildingAt`, `traversable(x,y,tribe)` (tech-gated terrain), `pushUnit` (push grid order S,W,N,E,SW,NW,NE,NE per `xPush/yPush`), `moveUnit` (clears view radius 1, +1 for mountain/battleship), `capture`, `launchExplorer`, road/port trade-network updates. Neighborhoods use `Vector2d.neighborhood(range, min, size)` — Chebyshev (8-connected) squares. Distances use Chebyshev (`chebychevDistance`, and `distance()` in levelgen is `max(|dx|,|dy|)`).

### 3.2 GameState (`src/core/game/GameState.java`)
- `Types.GAME_MODE gameMode`
- `Random rnd`
- `int tick` (one tick = full round of all players)
- `Board board`
- `boolean[] canEndTurn` (per tribe)
- `HashMap<Integer,ArrayList<Action>> cityActions`, `unitActions`; `ArrayList<Action> tribeActions` — computed available actions
- `boolean turnMustEnd`, `gameIsOver`, `levelingUp`
- `int computedActionTribeIdFlag` — dirty flag (-1 = stale)
- `TreeSet<TribeResult> ranking`

Turn cycle: `advance(action)` executes command; on END_TURN → `endTurn` (auto-Recover fresh units) → `gameOver` check → advance active tribe skipping LOSS tribes → `initTurn` (compute stars = Σ city production, refresh unit statuses, pacifist/war/stars counters). `initTurn` special: tick 0 sets stars to INITIAL_STARS; cities with an enemy unit on their tile produce nothing; temples grow.

`TribeResult` ranking key fields: id, winner, score, #techs, #cities, maxProduction, nWarsDeclared, nStarsSent (`GameState.computeGameRanking`).

---

## 4. Level generation (`src/core/levelgen/LevelGenerator.java`)

Java port of QuasiStellar's Polytopia-Map-Generator. Params via `init(mapSize, smoothing, relief, initialLand, tribes)`. Defaults used by `GameState.init`: `mapSize = DEFAULT_MAP_SIZE[nTribes-1]`, smoothing=3, relief=4, initialLand=0.5. `landCoefficient = (0.5 + relief)/9`. Output = array of `"terrainChar:resourceChar"` CSV lines consumed by `LevelLoader`.

Algorithm (in `generate()`):
1. **Seed land**: fill all DEEP_WATER; randomly convert `mapSize²·initialLand` tiles to PLAIN.
2. **Smooth** (×smoothing): a cell becomes ground if its radius-1 disk water-fraction ≤ landCoefficient, else deep water. (Uses `disk`, 8-conn.)
3. **Capital placement**: candidate = PLAIN tiles in interior `[2, mapSize-2)`. Greedily place each tribe's capital at the candidate maximizing the min Chebyshev distance to already-placed capitals (ties broken randomly). Writes CITY tile with tribe key as the "resource" field.
4. **Territory expansion**: flood-fill outward from each capital, tribes taking turns, claiming random valid (non-deep-water) neighbors until whole map assigned `tileOwner`.
5. **Forest/mountain**: for each PLAIN, roll one uniform; `< baseProb(FOREST)·tribeProb(FOREST,owner)` → FOREST; `> 1 - baseProb(MOUNTAIN)·tribeProb(MOUNTAIN,owner)` → MOUNTAIN.
6. **villageMap** classification: -1 water-far/mountain/border, 0 far, 1 border-expansion, 2 initial territory, 3 village.
7. **Shallow water**: any DEEP_WATER cross-adjacent to land → SHALLOW_WATER.
8. **Villages**: capitals marked 3; rings radius 1→2, radius 2→1. Then repeatedly place a village on the first `0` tile until no `0` tiles remain.
9. **Resources**: per-terrain probabilistic (using villageMap tier 2 at full prob, tier 1 at prob·BORDER_EXPANSION=1/3, via `proc()`): PLAIN→FRUIT or CROPS (mutually damped), FOREST→ANIMAL, SHALLOW→FISH, DEEP→WHALES, MOUNTAIN→ORE. Village tiles get VILLAGE terrain.
10. **Ruins**: count = round(mapSize²/40); water ruins = round(ruins/3). Placed on far/border/water tiles, dispersed (marks neighbors to avoid clustering).
11. **Post-generate capital fixups**: IMPERIUS guaranteed 2 FRUIT on PLAIN around capital; BARDUR guaranteed 2 ANIMAL on FOREST (`postGenerate`).

Probabilities live in `terrainProbs.json` (root dir): categories WATER, FOREST, MOUNTAIN, ORE, FRUIT, CROPS, ANIMAL, FISH, WHALES; each has `BASE` + a per-tribe multiplier for all 12 tribes. (E.g. HOODRICK FOREST 1.5, VENGIR ORE 2.0, BARDUR ANIMAL 2.0, KICKOO WATER 0.4/FISH 1.5.)

`LevelLoader` (`src/core/game/LevelLoader.java`) parses CSV `terrain:resource` tokens, creates capital cities + starting units, and handles the Luxidoor special-case level-3 walled capital.

---

## 5. Config / data files

- `terrainProbs.json` (root) — levelgen probabilities (loaded by `IO.readJSON`).
- `play.json`, `tournament.json`, `elites.json` (root) — run configs (agents, tribes, game mode, seeds, MCTS params). `play.json` selects Run Mode (PlayLG/PlayFile/Replay), players, tribes, level file, seeds.
- `starting_map_elites.txt` (root) — MAP-Elites seed data.
- `levels/*.csv` — hand-authored maps (SampleLevel, BalancedLevel4p, MinimalLevel variants, etc.). Format: comma-separated `terrainChar:resourceChar` per tile; city tiles are `c:<tribeKey>`.
- `data/Action Space samples.xlsx` — the only file in `data/` (spreadsheet, not runtime config).
- `img/` — sprites (terrain, unit/<name>/<tribeKey>.png, buildings, resources, weapons/effects). Referenced by enum imageFile fields; irrelevant to headless RL port.
- No CSV/JSON tech-tree or unit-stat data files — **all stats are hard-coded in `TribesConfig.java` and `Types.java`** (good: single source of truth to port).

---

## 6. Combat formula & notable rules

**Damage** (`AttackCommand.getAttackResults`):
```
attackForce  = ATK_att * (curHP_att / maxHP_att)
defenceForce = DEF_def * (curHP_def / maxHP_def) * defBonus
total        = attackForce + defenceForce
attackResult  = round( attackForce/total * ATK_att * 4.5 )
defenceResult = round( defenceForce/total * DEF_def * 4.5 )   // retaliation
```
defBonus: ×4.0 in city with walls; else ×1.5 if (fortify-capable unit in own city) OR (water tile + AQUATISM) OR (forest + ARCHERY) OR (mountain + MEDITATION). Retaliation only if attacker within defender's RANGE (Chebyshev) and defender survived. Melee attackers push into the tile on a kill.

**Costs/misc constants** (`TribesConfig`): ROAD_COST 2, CLEAR_FOREST gives +2 stars, GROW_FOREST 5, BURN_FOREST 5, PORT_TRADE_DISTANCE 4, CITY_CENTRE_POINTS 100, CITY_BORDER_POINTS 20, POINTS_PER_POPULATION 5, PROD_CAPITAL_BONUS 1, FIRST_CITY_CLEAR_RANGE 2, NEW_CITY_CLEAR_RANGE 1, EXPLORER_CLEAR_RANGE 1, CITY_EXPANSION_TILES 1. Diplomacy: ALLEGIANCE_MAX 60, ATTACK_REPERCUSSION -5, CAPTURE_REPERCUSSION -30, CONVERT_REPERCUSSION -5, MIN_STARS_SEND 15; send-stars capped (`canSendStars`: starsSent<30).

### Things surprising / divergent from real Polytopia (flag for port)
1. **INITIAL_STARS = 5** with a commented `//1000` debug value right next to it — verify before training.
2. **`Constants.PLAY_WITH_FULL_OBS = true`** by default — fog-of-war exists in data (`obsGrid`) but the default config disables partial observability for agents; several mechanics are *gated on partial-obs*: the tech-steal on meeting a tribe (`Tribe.meetTribe`), EYE_OF_GOD monument unlock, and resource masking. In full-obs these are effectively off. Decide deliberately for the port.
3. **Resource key 4 is unused** — keys are {0,1,2,3,5,6,7}. Don't assume contiguous indices when building C arrays.
4. **`units[][]` uses 0 as empty sentinel**, not -1 (unlike `tileCityId` which uses -1). Actor id 0 is never a unit.
5. **Max turns are small** (30 SCORE / 50 CAPITALS) and MAX_TURNS is a debug-tuned value — real Polytopia is typically 30 for the mobile default but these are experiment knobs.
6. **Population can go negative** (down to `-level`); negative population makes `getProduction()` return the negative population directly (a penalty), which is unusual.
7. **Temple points sequence** `{100,50,50,50,150}` and monument flat +400 are this implementation's scoring, not the mobile game's.
8. **Catapult DEF = 0**, MindBender ATK = 0 (cannot attack, only heal/convert) — handle divide-by-zero-adjacent cases (formula uses total = attack+defence, fine unless both zero).
9. **Water units carry `baseLandUnit`** and HP is inherited (constructed with maxHP=-1); embark/disembark replace the actor instance and reassign a new actorId path via `addUnit`. Non-trivial to model as flat structs — consider a `carriedType` field on a unit struct in C rather than object replacement.
10. **Board must be square**; `DEFAULT_MAP_SIZE[0] = -1` (index for 1 tribe) is invalid — minimum 2 tribes.
11. Knight "Persist": `addKill()` sets status back to ATTACKED enabling repeated attacks — special-case in the unit status FSM.
12. Diplomacy allegiance is a symmetric `int[nTribes][nTribes]` matrix with cascade effects (`checkConsequences` halves & inverts to third-party tribes) — present but only exercised by DECLARE_WAR/SEND_STARS/attack actions.

### Suggested flat layout for the C/PufferLib port
- Board: 5 planes `int8 terrain, int8 resource(+1 offset or -1 sentinel), int8 building, int32 unitId, int32 cityId`, plus per-tribe `uint8 obsGrid` plane and a `uint8 tradeNetwork` plane.
- Tribes: struct array with `stars, score, capitalId, nKills, pacifist, bool[24] techResearched, uint8[7] monumentStatus, dynamic citiesID/extraUnits lists`.
- Units: struct array indexed by actorId with the Unit fields above + `carriedType` + `status` enum.
- Cities: struct array with the City fields + fixed-cap unit id list (cap = level+1) + building list.
- Constants: transcribe `TribesConfig`/`Types` tables verbatim (all hard-coded, no external data except `terrainProbs.json`).
