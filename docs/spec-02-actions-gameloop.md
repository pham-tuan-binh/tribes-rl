# The Battle of Polytopia (GAIGResearch/Tribes) — Actions, Combat & Game Loop Reference

Repo root: `reference/Tribes`. Java, package `core.*` (engine) and `players.*` (agents). All formulas below are verbatim from source; file paths cited inline.

Key constants live in `src/core/TribesConfig.java`; enums/dispatch in `src/core/Types.java`; hard caps in `src/core/Constants.java`. Note: **`Constants.PLAY_WITH_FULL_OBS = true`** and **`GUI_FORCE_FULL_OBS = true`** in the shipped build — several stochastic bonuses (tech-on-meet, EYE_OF_GOD) are gated behind partial-obs and are therefore **disabled** in the default config. Flag these when porting.

---

## 1. Complete Action Catalog

Three families: **CityAction** (`super.cityId`, `targetPos`), **UnitAction** (`super.unitId`), **TribeAction** (`super.tribeId`). Base classes: `src/core/actions/{Action.java,cityactions/CityAction.java,unitactions/UnitAction.java,tribeactions/TribeAction.java}`. Every action has `isFeasible(GameState)` (in the action class) and an `ActionCommand.execute(...)` (effects) dispatched via `Types.ACTION.getCommand()` (`Types.java:823`).

### City actions (`src/core/actions/cityactions/`)

| Action | Params | Preconditions (`isFeasible`) | Effects (`…Command.execute`) |
|---|---|---|---|
| **Build** | `buildingType`, `targetPos` | Tile in city tiles & empty of building; enough stars (`buildingType.getCost()`); tech researched; terrain in `terrainRequirements`; resource constraint (MINE→ORE, FARM→CROPS); adjacency constraint (CUSTOMS→PORT, WINDMILL→FARM, FORGE→MINE, SAWMILL→LUMBER_HUT) in neighborhood(1); uniqueness for unique bldgs; monuments must be `AVAILABLE` in tribe. `Build.java` | Subtract stars; place building; clear tile resource; add Building/Temple to city (updates pop/prod bonuses); PORT→`buildPort` (trade net); monument→`monumentIsBuilt`; LUMBER_HUT converts FOREST→PLAIN. `BuildCommand.java` |
| **Spawn** | `unit_type` (fixed to city pos) | Type `spawnable()` (no BOAT/SHIP/BATTLESHIP/SUPERUNIT); stars ≥ unit cost; `city.canAddUnit()` (units < level+1); city tile empty of unit; tech researched. `Spawn.java` | Create unit at city; subtract stars; `tribe.addScore(unit points)`. `SpawnCommand.java` |
| **LevelUp** | `bonus` (`CITY_LEVEL_UP`) | `city.canLevelUp()` (population ≥ population_need) AND `bonus.validType(level)`. `LevelUp.java` | addScore(levelUpPoints), `city.levelUp()`, then per bonus. `LevelUpCommand.java` |
| **ResourceGathering** | `resource`, `targetPos` | Resource present at target; stars ≥ resource cost; tech (ANIMAL→HUNTING, FISH→FISHING, WHALES→WHALING, FRUIT→ORGANIZATION). `ResourceGathering.java` | Remove resource; subtract cost; FISH/ANIMAL/FRUIT→`addPopulation(bonus)`; WHALES→`addStars(10)`. `ResourceGatheringCommand.java` |
| **ClearForest** | `targetPos` | Tile FOREST & owned by this city; FORESTRY researched. `ClearForest.java` | FOREST→PLAIN; `+2` stars (`CLEAR_FOREST_STAR`). `ClearForestCommand.java` |
| **BurnForest** | `targetPos` | Tile FOREST & owned by city; stars ≥ 5; CHIVALRY. `BurnForest.java` | FOREST→PLAIN, set resource CROPS; `-5` stars. `BurnForestCommand.java` |
| **GrowForest** | `targetPos` | Tile PLAIN & owned by city; stars ≥ 5; SPIRITUALISM. `GrowForest.java` | PLAIN→FOREST, clear resource; `-5` stars. `GrowForestCommand.java` |
| **Destroy** | `targetPos` | Building present & owned by city; CONSTRUCTION. `Destroy.java` | Remove building (PORT also removed from trade net); reverses building effects. `DestroyCommand.java` |

`CITY_LEVEL_UP` options by level (`Types.java:419`): L1→{WORKSHOP,EXPLORER}, L2→{CITY_WALL,RESOURCES}, L3→{POP_GROWTH,BORDER_GROWTH}, L≥4→{PARK,SUPERUNIT}.

### Unit actions (`src/core/actions/unitactions/`)

| Action | Params | Preconditions | Effects |
|---|---|---|---|
| **Move** | `destination` | `unit.canMove()`; dest empty of unit; A* path exists via `StepMove`. `Move.java`/`MoveFactory.java` | `board.moveUnit` (clears fog, updates trade net); auto embark (land→PORT water) / disembark (water unit→land); status→MOVED. `MoveCommand.java` |
| **Attack** | `targetId` | target exists; `attacker.canAttack()`; not MIND_BENDER; target visible & within `attacker.RANGE` (Chebyshev). `Attack.java` | Combat (§2); status→ATTACKED; diplomacy −5 (×2 on kill). `AttackCommand.java` |
| **Capture** | `targetCityId`, `captureType` (CITY/VILLAGE) | Unit FRESH; on a VILLAGE tile, or standing on enemy CITY tile not owned by unit tribe. `Capture.java` | `board.capture(...)`; status→FINISHED; on CITY: transfer pointsWorth, diplomacy −30. `CaptureCommand.java` |
| **Convert** | `targetId` | Unit is MIND_BENDER; target exists & enemy; `canAttack()`; in range. `Convert.java` | Target switches tribe (becomes extra unit), diplomacy −5; MB status→ATTACKED, target→FINISHED. `ConvertCommand.java` |
| **HealOthers** | (none) | MIND_BENDER; `canAttack()`; ≥1 friendly damaged unit within RANGE. `HealOthers.java` | Heal each friendly in range by `MINDBENDER_HEAL=4` (capped at maxHP); MB→ATTACKED. `HealOthersCommand.java` |
| **Recover** | (none) | Unit FRESH; 0 < currentHP < maxHP. `Recover.java` | +2 HP (`RECOVER_PLUS_HP`), +2 more if inside own city borders (`RECOVER_IN_BORDERS_PLUS_HP`); status→FINISHED. `RecoverCommand.java` (auto-applied at end of turn to all FRESH units — §3) |
| **MakeVeteran** | (none) | not SUPERUNIT; kills ≥ 3 (`VETERAN_KILLS`); not already veteran. `MakeVeteran.java` | veteran=true; maxHP += 5 (`VETERAN_PLUS_HP`); heal to full. `MakeVeteranCommand.java` |
| **Upgrade** | actionType (UPGRADE_BOAT/SHIP) | BOAT+SAILING+stars≥SHIP.cost(5), or SHIP+NAVIGATION+stars≥BATTLESHIP.cost(15). `Upgrade.java` | Replace unit with next naval tier, preserve HP/kills/veteran/status; subtract cost. `UpgradeCommand.java` |
| **Disband** | (none) | Unit FRESH; FREE_SPIRIT researched. `Disband.java` | Refund `COST/2` (floor) stars; remove unit; subtract unit points from score. `DisbandCommand.java` |
| **Examine** | `bonus` (rolled at execute) | Unit FRESH; tribe has ≥1 city; standing on RUINS resource. `Examine.java` | Random `EXAMINE_BONUS` (§5); status→FINISHED. `ExamineCommand.java` |

### Tribe actions (`src/core/actions/tribeactions/`)

| Action | Params | Preconditions | Effects |
|---|---|---|---|
| **ResearchTech** | `tech` | stars ≥ `tech.getCost(numCities, tree)`; `tree.isResearchable` (parent researched, not already). `ResearchTech.java` | Subtract cost; mark researched; addScore(`tier*100`); flag all-researched→TOWER_OF_WISDOM available. `ResearchTechCommand.java` |
| **BuildRoad** | `position` | `tribe.canBuildRoads()` (ROADS + stars≥2); `board.canBuildRoadAt` (visible, PLAIN/FOREST/VILLAGE, neutral or own-city tile, no existing road, no enemy unit). `BuildRoad.java` | −2 stars (`ROAD_COST`); add road to trade net. `BuildRoadCommand.java` |
| **DeclareWar** | `targetID` | allegiance[self][target] > −30 (i.e. > −ALLEGIANCE_MAX/2) AND not already declared war this turn. `DeclareWar.java` | Set allegiance to −30, `hasDeclaredWar=true`, increment war counter, propagate consequences. `DeclareWarCommand.java` |
| **SendStars** | `numStars`, `targetID` | `canSendStars` (starsSent<30, numStars<stars, stars>0) AND `numStars ≤ MIN_STARS_SEND(15)`. `SendStars.java` | Move stars self→target; addScore(numStars); raise allegiance. `SendStarsCommand.java` |
| **EndTurn** | (none) | `gs.canEndTurn(tribeId)` (false while a city is leveling up). `EndTurn.java` | Sets `turnMustEnd=true` (the game loop then finalizes the turn). `EndTurnCommand.java` |

**Note on units:** unit spawn/veteran/attack constants per unit type are in `TribesConfig.java:7-111`. Superunit: 40 HP, 5 ATK/4 DEF. Naval units carry a `baseLandUnit` (the embarked land unit) — embark converts land→BOAT, disembark restores base type; upgrade chains BOAT→SHIP→BATTLESHIP (`Board.embark/disembark`, `UpgradeCommand`).

---

## 2. Combat Resolution (verbatim pseudocode)

From `AttackCommand.getAttackResults` and `execute` (`src/core/actions/unitactions/command/AttackCommand.java`). Constants: `ATTACK_MODIFIER=4.5`, `DEFENCE_BONUS=1.5`, `DEFENCE_IN_WALLS=4.0` (`TribesConfig.java:117-119`).

```
# Damage computation
attackForce  = attacker.ATK * (attacker.currentHP / attacker.maxHP)
defenceForce = target.DEF   * (target.currentHP   / target.maxHP)
accelerator  = 4.5   # ATTACK_MODIFIER

# Defence bonuses (multiply defenceForce), from target's tile & target tribe's tech:
targetTerrain = terrainAt(target.pos)
if targetTerrain == CITY and targetTribe controls that city:
    if city.hasWalls():                 defenceForce *= 4.0     # DEFENCE_IN_WALLS
    elif target.type.canFortify():      defenceForce *= 1.5     # DEFENCE_BONUS
elif (targetTerrain == MOUNTAIN and targetTribe has MEDITATION)
     or (targetTerrain.isWater() and targetTribe has AQUATISM)
     or (targetTerrain == FOREST   and targetTribe has ARCHERY):
    defenceForce *= 1.5     # DEFENCE_BONUS

totalDamage   = attackForce + defenceForce
attackResult  = round( (attackForce  / totalDamage) * attacker.ATK * 4.5 )   # dmg TO target
defenceResult = round( (defenceForce / totalDamage) * target.DEF   * 4.5 )   # retaliation dmg TO attacker
```

`canFortify()` = WARRIOR, RIDER, ARCHER, DEFENDER, SWORDMAN, KNIGHT (`Types.java:596`). Wall bonus only applies if the city has walls AND is controlled by the target's tribe; otherwise fortify bonus applies to fortify-capable units on a friendly city tile.

```
# Resolution (execute)
attacker.status = ATTACKED     # transitionToStatus; see status machine
attackerTribe.resetPacifistCount()
diplomacy.updateAllegiance(-5, attacker, target); checkConsequences(...)

if target.currentHP <= attackResult:                 # target dies
    diplomacy.updateAllegiance(-5, ...) again          # extra -5 on kill
    attacker.addKill(); attackerTribe.addKill()
    killUnit(target)                                    # removes unit, subtract target points from its tribe score
    if attacker.type in {DEFENDER,SWORDMAN,RIDER,WARRIOR,KNIGHT,SUPERUNIT}:  # melee
        board.tryPush(attacker into target's tile)      # melee advances onto killed tile
else:
    target.currentHP -= attackResult
    # Retaliation: only if attacker within target.RANGE (Chebyshev)
    if chebyshev(attacker.pos, target.pos) <= target.RANGE:
        attacker.currentHP -= defenceResult
        if attacker.currentHP <= 0:
            target.addKill(); targetTribe.addKill(); killUnit(attacker)
```

Edge cases: ranged units (ARCHER R2, CATAPULT R3, naval R2) attacking from outside the target's range take **no retaliation**. Melee kill triggers advance-move (via `tryPush`, respects mountain/water rules). `addKill()` on a KNIGHT resets its status to ATTACKED, enabling chain-attacks (`Unit.addKill`, `Unit.java:63`).

**Veteran promotion:** manual action `MakeVeteran` (not automatic) at ≥3 kills → +5 maxHP, full heal (`MakeVeteranCommand`).

---

## 3. Turn / Phase State Machine

Game loop: `src/core/game/Game.java` (`run`→`tick`→`processTurn`). Forward-model equivalent: `GameState.advance()` (`src/core/game/GameState.java:311`).

**Tick** = one full round (all tribes). Turn order = index order in tribes array (`Game.tick`, line 277). Tribes with `winner != INCOMPLETE` are skipped. `gs.incTick()` after all tribes act.

**Per-tribe turn (`processTurn`, Game.java:320):**
1. `gs.initTurn(tribe)` — start-of-turn (below).
2. `gs.computePlayerActions(tribe)` — enumerate all legal actions (§4).
3. Loop: `action = agent.act(obs, timer)`; `gs.next(action)`; recompute actions. Continues while `!gs.isTurnEnding()` AND `gs.existAvailableActions(tribe)`. A city leveling-up forces `canEndTurn=false` and restricts the action set until resolved.
4. On EndTurn (or no actions): `gs.endTurn(tribe)`.

**`initTurn` (GameState.java:412):**
- **Star income:** `acumProd = Σ city.getProduction()` over cities (a city with an *enemy* unit on its center tile produces nothing). On tick 0, stars set to `INITIAL_STARS=5`; else `stars += max(0, acumProd)`.
- **Temples grow:** each temple adds turn points to tribe score & city pointsWorth.
- **Units reset:** all units → FRESH, except units that were PUSHED last turn → start MOVED.
- Pacifist counter++ (if MEDITATION; 5 turns→ALTAR_OF_PEACE), reset starsSent, `hasDeclaredWar=false`.

**`endTurn` (GameState.java:376):** every unit still in FRESH status auto-executes `Recover` (heal +2, +2 more in own borders).

**City growth:** No automatic per-turn population growth. Population is added only by ResourceGathering, buildings, POP_GROWTH level-up, temple/port bonuses, and trade-network connections (`Tribe.updateNetwork`). A city levels up **only** via the explicit LevelUp action once `population ≥ population_need`. `levelUp()`: `level++`, `population -= population_need`, `population_need = level+1` (`City.java:275`). `getProduction() = level + production(+1 capital bonus)` when pop ≥ 0, else = population (negative) (`City.java:255`).

**Win conditions (`GameState.gameOver`, line 580):** two modes (`Types.GAME_MODE`):
- **CAPITALS:** a tribe wins when it controls **all** capital city IDs. Max turns `MAX_TURNS_CAPITALS = 50` (`Constants.java:37`).
- **SCORE:** game ends when ≤1 non-loss tribe remains, or at `MAX_TURNS = 30`.
- On end (or `tick > maxTurns`): ranking computed, rank 1 → WIN, rest → LOSS.

**Ranking / tie-break (`TribeResult`, `src/core/game/TribeResult.java`):** `TreeSet<TribeResult>` sorted by score, then techs, cities, production, wars, stars-sent. A tribe with 0 cities after a capture is set to LOSS (`Board.capture`→`manageLoss`).

---

## 4. Action Enumeration Mechanics & Branching Factor

`GameState.computePlayerActions(tribe)` (line 170) builds three maps: `cityActions{cityId→[…]}`, `unitActions{unitId→[…]}`, `tribeActions[…]`. Caching: `computedActionTribeIdFlag` skips recompute if unchanged; `next()` sets it to −1 (dirty). `getAllAvailableActions()` flattens all three.

**Builders (Composite→Factory pattern):**
- `CityActionBuilder` (`factory/CityActionBuilder.java`): if any LevelUp available, returns **only** LevelUp actions (and sets `levelingUp` → whole state restricted, cannot EndTurn). Otherwise runs Build, BurnForest, ClearForest, Destroy, GrowForest, ResourceGathering, Spawn factories.
- `UnitActionBuilder`: Upgrade always; if `unit.isFinished()` stop; else Attack, Capture, Convert, Disband, Examine, HealOthers, MakeVeteran, Move, Recover.
- `TribeActionBuilder`: BuildRoad, ResearchTech, DeclareWar, SendStars, EndTurn.

Each `ActionFactory.computeActionVariants(actor, gs)` **instantiates one action object per grounded parameter and calls `isFeasible` to filter** (e.g. `BuildFactory` loops city tiles × all BUILDING types; `MoveFactory` runs a `Pathfinder`+`StepMove` from the unit and emits a Move per reachable empty tile; `SpawnFactory` loops all UNIT types; `ResearchTechFactory` loops all TECHNOLOGY; `AttackFactory`/`ConvertFactory` scan `neighborhood(RANGE)` for enemy units; `SendStarsFactory` loops targets × 1..min(stars,15)).

**Branching factor:** The `SendStarsFactory` double-loop (targets × star amounts up to 15) and `BuildRoadFactory` (every legal road tile) dominate; `BuildFactory` is city-tiles × building-types. The repo instrumented this (`Game.updateBranchingFactor`, `Agent.actionsPerUnit/actionsPerGameState`) and produced `py/plot/branchingFactorStep.png` etc. Empirically the per-decision branching factor runs into the **hundreds and grows over the game**. For a C/RL port you will want to (a) cap SendStars amounts to a few discrete choices, and (b) restrict road/build tiles, to keep the action space tractable.

---

## 5. Stochasticity Inventory

RNG: single `java.util.Random` seeded from `GAME_SEED` (`Game.init`, line 75); level generator uses its own `Random(levelgen_seed)` (`LevelGenerator.java:46`). **Crucially, `GameState.copy()` gives the copy a brand-new `new Random()` (unseeded)** (`GameState.java:527`) — so forward-model rollouts are NOT reproducible from the game seed. Port this deliberately (you likely want a seeded/splittable RNG per copy for RL).

Stochastic elements:
1. **Map generation** (`src/core/levelgen/LevelGenerator.java`): terrain via `rnd.nextDouble()` against per-tribe probabilities in `terrainProbs.json`; village placement; `randomInt`. Map size table `DEFAULT_MAP_SIZE` (`TribesConfig.java:230`).
2. **Ruins / Examine bonus** (`ExamineCommand`, `Types.EXAMINE_BONUS.random`): uniform over {SUPERUNIT, RESEARCH(+random tech), POP_GROWTH(+3), EXPLORER, RESOURCES(+10 stars)}. RESEARCH re-rolls if everything researched. SUPERUNIT on water spawns a BATTLESHIP instead.
3. **Explorer movement** (`Board.launchExplorer`): 15 steps of random traversable neighbor picks.
4. **City capture unit redistribution** (`Board.moveOneToNewCity`/`moveAllFromCity`): `Collections.shuffle(cities, rnd)`.
5. **Random tech at random** (`TechnologyTree.researchAtRandom`) — used by Examine RESEARCH.
6. **Combat is deterministic** (no dice — pure formula in §2). Contrast with real Polytopia.
7. Partial-obs-only stochastics (disabled at default `PLAY_WITH_FULL_OBS=true`): tech-gift on meeting a tribe (`Tribe.meetTribe`), EYE_OF_GOD monument on full map reveal.

---

## 6. `py/` Directory Contents

Not Python bindings to the engine — two standalone helper toolsets:
- **`py/levelgen/`** — a **pure-Python reference implementation of the map generator** (`app.py`, `utils.py`), ported from QuasiStellar's Polytopia-Map-Generator. Hard-codes map_size/initial_land/smoothing/relief and an inline `terrain_probs` dict per tribe (mirrors `terrainProbs.json`). Useful as a cross-check for the TS/C map-gen port.
- **`py/plot/`** — matplotlib analysis scripts + pre-rendered PNGs for the research paper (branching factor plots etc.). No engine hooks.

There is no Python↔Java FFI. A fresh C/PufferLib port must reimplement the engine; `py/levelgen` is the only reusable Python asset.

---

## 7. Baseline Agent Survey (`src/players/`)

All extend `players.Agent` (abstract `Action act(GameState, ElapsedCpuTimer)`). Turn = repeated single-action `act` calls until EndTurn.

Trivial baselines: **DoNothingAgent** / **EndTurnAgent**; **RandomAgent** (uniform over legal actions); **SimpleAgent** (hand-coded rule-based; reasonable scripted opponent).

Search/planning agents (use the forward model): **OneStepLookAheadAgent** (osla/), **MonteCarloAgent** (mc/), **MCTSPlayer** (mcts/, UCT K=√2, ROLLOUT_LENGTH=10), **RHEAAgent** (rhea/, POP_SIZE=100, INDIVIDUAL_LENGTH=20), **OEPAgent** (oep/), **EMCTSAgent** (emcts/), **PortfolioMCTSPlayer** (portfolioMCTS/ + ~60 scripts in `players/portfolio/scripts/` like `AttackMaxDamageScr`, `MoveToCaptureScr` — strongest agent family).

Heuristics (`players/heuristics/`): `TribesSimpleHeuristic`, `TribesDiffHeuristic`, `TribesEntropyHeuristic` — reusable as reward/eval signals in RL. Recommended eval ladder: DoNothing < Random < SimpleAgent < OSLA < MCTS/RHEA < PortfolioMCTS.

---

### Porting gotchas worth carrying into TS/C
- Copy-time RNG reset (§5) — decide rollout determinism policy explicitly.
- `PLAY_WITH_FULL_OBS=true` disables fog **and** two monument/tech bonuses; `copy(playerId)` implements fog masking + resource masking (gated by ORGANIZATION/CLIMBING/FISHING) and hides enemy tribe internals — only exercised when full-obs is off.
- Movement (`StepMove.java`): fractional step costs (road halves cost, min 0.5), embark/disembark consume all remaining MP, **zone-of-control** (enemy adjacent to destination) consumes all remaining movement, mountains/forest cost = all remaining MP for land units, terrain traversability gated by CLIMBING/SAILING/NAVIGATION (`Board.traversable`). `MOV`: warrior/most=1, rider=2, knight=3, ship/battleship=3.
- Unit turn-status FSM (`Unit.canTransitionTo`, `Unit.java:96`) encodes per-type skills: Dash (move+attack: warrior/swordman/archer/naval/superunit), Escape (attack then move: rider), Persist (knight re-attacks on kill), single-action (defender/catapult/mindbender). This must be ported faithfully — it governs `canMove()`/`canAttack()`/action availability.
