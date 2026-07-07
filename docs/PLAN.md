# polytopia-rl — Port & Training Plan

Goal: rebuild The Battle of Polytopia (using GAIGResearch/Tribes as the rules spec) as
1. a **fast headless RL environment** trained with PufferLib self-play, and
2. a **playable website** where humans play against the trained agent, fully client-side.

Companion specs (read these before implementing):
- `spec-01-data-model.md` — enums, stats, board layout, map generation
- `spec-02-actions-gameloop.md` — action catalog, combat formulas, turn machine, stochasticity
- `spec-03-pufferlib.md` — PufferLib 4.0 native env API, self-play, masking, web path

---

## 1. Architecture: one C core, three consumers

```
                ┌─────────────────────────────────────────┐
                │  core/polytopia.h  (C11, zero deps)      │
                │  state · mapgen · legal actions · step    │
                └───────┬──────────────┬───────────────────┘
                        │              │
        native (clang -O3)        emscripten (emcc -O3)
                        │              │
      ┌─────────────────┴──┐   ┌───────┴────────────────────┐
      │ puffer/binding.c    │   │ web/ (TS + PixiJS UI)      │
      │ → pufferlib _C.so   │   │ core.wasm + puffernet.wasm │
      │ → puffer train      │   │ → human vs agent, hotseat  │
      └─────────────────────┘   └────────────────────────────┘
```

The engine is a single header (`polytopia.h`) in PufferLib Ocean style: POD `Env` struct, fixed-size arrays, zero allocation in `step`, `rand_r`-style seeded RNG. **No game logic anywhere else.** The web app's TS layer is rendering/input only; the trained policy runs in the browser through `puffernet.h` (PufferLib's C inference lib, ~1100 lines) compiled into the same WASM module, loading exported `.bin` weights.

Why C (vs Rust/TS): PufferLib 4.0's fast path *is* a C header + macro binding statically linked into their CUDA trainer; their chess env (our closest blueprint) and their WASM browser demos are all this exact shape. Fighting that with another language buys nothing.

## 2. v1 game scope

Faithful port of Tribes rules **with these deliberate deviations**:

| Decision | v1 choice | Rationale |
|---|---|---|
| Players | 2 | chess-style self-play first; N-player later |
| Map | 11×11 (Tribes default for 2 tribes), procgen port of Tribes levelgen | fixed obs size |
| Mode | CAPITALS (domination, 50-turn cap) | clean ±1 win/loss reward |
| Observability | full obs (Tribes default) | fog-of-war is a v2 flag; engine keeps per-tribe `obsGrid` from day 1 |
| Diplomacy (DeclareWar/SendStars) | **cut** | not in real Polytopia, degenerate branching (targets × 15 star amounts), Tribes-only invention |
| Tribes | all 12 (they differ only in starting tech/unit, Luxidoor L3 capital, mapgen probs) | cheap to include |
| Everything else | verbatim: combat formula (×4.5 modifier, walls ×4.0, terrain ×1.5), unit status FSM (Dash/Escape/Persist), tech costs `4 + tier·nCities`, city econ, ruins, embark/disembark, push rules | see specs 01/02 |

Engine-level knobs (compile/ini-time): map size, n players, mode, fog, max turns.

## 3. RL formulation

**One env tick = one atomic decision** (not one game turn). Both players occupy live agent slots every tick (chess pattern); the non-active player's mask forces a PASS action.

**Action space — single masked Discrete head, reused across 3 phases** (chess's pick/place pattern generalized). Head size `= S² + 72` (11×11 → 193):

| Phase | Legal indices | Meaning |
|---|---|---|
| SELECT | tile ids with own actionable unit/city, `END_TURN`, tribe-slot | pick what acts |
| VERB | `S²..S²+71`: move, attack, capture, recover, disband, examine, heal, veteran, upgrade, convert, gather, clear/burn/grow forest, build×13, spawn×8, levelup×8, research×24, road | pick what it does |
| TARGET | tile ids (move dests, attack targets, build/road tiles) | pick where (skipped for verbs with no target) |

`#define MY_ACTION_MASK (S*S + 72)` — the engine writes exact legality every tick; masking flows through PufferLib's native sampler/entropy/learner. A LevelUp-pending city restricts the mask to its two options, exactly like Tribes' `levelingUp` lock.

**Observation — `ByteTensor`, flat, from acting player's perspective** (~1.5 KB at 11×11):
- per-tile planes (11×11 each): terrain, resource, building, road/trade, city-owner(+level via city tile), unit-type, unit-owner, unit-HP (scaled), unit-status
- selection planes: currently-selected actor, current phase one-hot
- scalars: own/enemy stars (capped), 24 tech bits ×2, scores, city counts, tick/maxTurns, mode flags

No CNN in the native trainer — flat encoder + MinGRU recurrence (this is what chess trained on at 1e12 steps; the MinGRU also future-proofs fog-of-war). Custom CUDA encoder is the escape hatch if flat underperforms.

**Reward:** terminal +1 win / −1 loss (split on capital capture or turn-cap ranking); optional small shaping (Δscore × 1e-4, configurable in ini, anneal to 0).

**Self-play:** `[selfplay] enabled=1` — PufferLib's pool gives ELO tracking, snapshot pool, winrate-gated swaps, frozen banks for free. Env struct carries `int tag; int boundary_reached;` + `MY_USES_TAGS`/`MY_USES_PERM`.

**Eval ladder** (ported from Tribes baselines): Random → SimpleAgent (rule-based, port its heuristics) → (optional) 1-ply greedy. `puffer match` for checkpoint-vs-checkpoint ELO.

## 4. Correctness strategy (the port-killer risk)

The engine must match Tribes' semantics or the "spec" is fiction. Three layers:

1. **Golden traces:** patch a tiny Java runner (`tools/java_trace/`) that plays seeded games (Random + MCTS agents), dumping per-step JSON: full state hash, sorted legal-action set, chosen action, post-state hash. The C engine replays the action stream and must reproduce state + legal-action sets exactly. (Stochastic actions: inject the Java-rolled outcome via a test hook.)
2. **Unit tests in C:** combat table tests (every unit pair × bonus combos vs formula), status-FSM transition table, movement/ZoC/embark cases, tech costs, city econ, mapgen statistical checks vs `py/levelgen` reference.
3. **Fuzz invariants:** random legal playouts for millions of steps under ASan/UBSan (`build.sh --local` style): no crashes, conservation invariants (unit counts, star non-negativity, board/registry consistency), games terminate.

## 5. Repo layout

```
polytopia-rl/
  docs/                    # this plan + specs
  core/
    polytopia.h            # the engine (single header, Ocean style)
    constants.h            # transcribed TribesConfig/Types tables
    mapgen.h               # levelgen port (+ terrain_probs baked in)
    tests/                 # C unit tests, golden-trace replayer, fuzzer
  puffer/
    binding.c              # ~50 lines of macros + init/log
    polytopia.ini          # env + train + selfplay config (start from chess.ini)
    export_weights.py      # checkpoint → flat .bin for puffernet
  web/
    src/                   # TS + PixiJS: renderer, input, UI panels
    wasm/                  # emcc build of core + puffernet + C ABI bridge
    public/weights/        # trained policy .bin
  tools/java_trace/        # patched Tribes golden-trace dumper
  reference/Tribes/        # upstream clone (read-only spec)
```

PufferLib integration = drop `core/`+`puffer/binding.c` as `ocean/polytopia/` in a PufferLib checkout (or submodule PufferLib and symlink), `./build.sh polytopia`, `puffer train polytopia`.

## 6. Milestones

**M0 — Scaffold + constants (small)**
Transcribe every table from spec-01 into `constants.h`; state structs; repo/build scaffolding (Makefile: native, asan, wasm targets). ✓ when: constants unit-tested against spec values.

**M1 — Engine core (the big one)**
Board/actors, mapgen port, legal-action enumeration, `apply_action`, turn machine, win conditions. Golden-trace harness green on ≥100 seeded Java games; fuzzer clean for 10M+ steps. ✓ when: bit-faithful replay + ~1M+ random steps/sec/core single-thread (sanity perf bar).

**M2 — PufferLib env**
`binding.c` (2 agents, masked head, byte obs), ini, first training run on the `lk` box (`ssh lk`: RTX 5090 32 GB, CUDA 13.3, 24 cores — Mac is dev-only since the native trainer requires CUDA). ✓ when: agent beats Random >95% and SimpleAgent-port >60%.

**M3 — Self-play at scale**
Selfplay pool config, reward-shaping ablation, sweeps (`puffer sweep`), eval ladder + ELO tracking. ✓ when: monotonic ELO curve and clearly super-SimpleAgent play; export weights via puffernet `.bin` and verify C inference matches torch logits.

**M4 — The website (one page, two modes)**
Design brief: *super simple* — the site IS the game, nothing else. Two modes behind one toggle:
- **Agents**: spectate agent-vs-agent self-play with a speed slider from "watchable" to "as fast as possible" (sim decoupled from render; at max speed, run the WASM sim unthrottled and paint at rAF).
- **Human**: play against the agent(s) — human input drives the same SELECT/VERB/TARGET machine the agent uses (click actor → click verb → click target).

Stack: emcc build of the core + a small exported C ABI (`init(seed) / legal_mask() / apply(action) / state_view()` + `-sMODULARIZE`); plain TypeScript + Canvas 2D (no framework, no rendering library — flat-shaded tiles; simple, dependency-light code suitable for open-sourcing). Agent inference via puffernet compiled into the same WASM module, weights fetched as `.bin`; difficulty = checkpoint choice / sampling temperature. ✓ when: both modes work in a browser, mobile-friendly, <5 ms/agent decision.

**M5 — Multiplayer (3–4 players)**
Committed (not stretch): engine already supports MAX_PLAYERS 4; size obs/action space for the max map (masked down), retrain with 3–4 agent slots per env, and extend both website modes to N players. ✓ when: 4-player games watchable and playable in the browser.

**M6 — Stretch**
Fog-of-war training (engine already tracks `obsGrid`; MinGRU handles memory), bigger maps, replay viewer, public leaderboard vs the bot. (Special tribes: explicitly out of scope.)

**Open-source posture:** keep the code simple and self-contained for a later public release — C engine with zero deps, no-framework web client, MIT license, this docs/ folder as the documentation.

## 7. Known risks & mitigations

- **PufferLib 4.0 API brittleness** (one env per `_C.so`, awk-scraped macros, young API): pin a commit; keep `binding.c` trivial so rebases are cheap.
- **CUDA-only native trainer & masking** (torch fallback lacks masking): masking is non-negotiable for this action space → training runs on the `lk` RTX 5090 box from M2 onward; local Mac dev loop = C tests + random-playout benchmarks, not training. (Blackwell sm_120 needs recent CUDA/torch — CUDA 13.3 on lk covers it.)
- **Branching/credit-assignment**: 3-phase decomposition keeps heads small, but turns are long (dozens of ticks). If learning stalls: stronger shaping early, curriculum (tiny maps, shorter caps — `MinimalLevel` style), or verb-head pruning.
- **Rules drift vs real Polytopia**: Tribes ≠ mobile game (scoring, diplomacy, some constants). We port *Tribes* faithfully first (testable), then optionally patch toward real-Polytopia feel behind config flags (spec-01 §6 lists divergences).
- **Java golden traces need stochastic-outcome injection** — Tribes' `GameState.copy()` resets RNG unseeded; our harness records rolled outcomes rather than trying to match Java's RNG.
```
