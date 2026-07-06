# PufferLib 4.0 (mid-2026) — Integration Reference

Verified against the live repo (default branch **`4.0`**, version 4.0.0, ~6.1k stars). Docs: https://puffer.ai/docs.html, repo: https://github.com/PufferAI/PufferLib (note: `main` branch does not exist; use `4.0`).

**Big picture vs 3.x:** PufferLib 4.0 dropped the Gymnasium/PettingZoo emulation path as the main road. The stack is now: C env (`.h`) + macro-based binding (`binding.c` + `src/vecenv.h`) statically linked into a per-env Python extension `pufferlib/_C.so` that contains the **entire trainer written in CUDA** (`src/pufferlib.cu`, ~2.3k lines). Python (`pufferlib/pufferl.py`) is just config/CLI/logging. A PyTorch fallback backend (`torch_pufferl.py`, flag `--slowly`) exists. Repo is 80% C.

## 1. Native environment API (the fast path)

**File layout per env** (e.g. `ocean/target/`, `ocean/chess/`, `ocean/moba/`):
- `env.h` — all game logic + raylib rendering. Compiled into training builds.
- `binding.c` — ~20–220 lines of metadata + init glue; `#include "vecenv.h"` after macros.
- `env.c` — *optional* standalone demo `main()` (desktop + web builds only).
- `config/env.ini` — hyperparameters, env kwargs, selfplay config. Registration = matching directory name + ini file.

**No ctypes, no Cython, no code generation.** `binding.c` is compiled by `build.sh <env>` into `build/libstatic_<env>.a`, then linked with `src/bindings.cu` (pybind11 + CUDA) into `pufferlib/_C.{abi}.so`. One env per build.

**Env struct convention** (from `ocean/target/target.h`): a plain struct whose buffer pointers are *assigned by the framework* into giant contiguous cross-env arrays:

```c
typedef struct {
    Log log;                 // struct of floats; auto-aggregated
    Client* client;          // raylib render state, NULL headless
    float* observations;     // framework-assigned slice of global buffer
    float* actions;          // actions are ALWAYS float (even discrete)
    float* rewards;          // 1 float per agent
    float* terminals;        // 1 float per agent
    int num_agents;
    unsigned int rng;
} Target;
```

**Required functions:** `void c_reset(Env*)`, `void c_step(Env*)`, `void c_render(Env*)`, `void c_close(Env*)`, plus binding-side `void my_init(Env*, Dict* kwargs)` and `void my_log(Log*, Dict* out)`. `c_step` advances *all* agents in the env one tick, writing obs/rewards/terminals in place (rewards/terminals are zeroed by the framework before each step).

**binding.c is macro-driven** (`ocean/target/binding.c`, verbatim):

```c
#include "target.h"
#define OBS_SIZE 28
#define NUM_ATNS 2
#define ACT_SIZES {9, 5}          // MultiDiscrete!
#define OBS_TENSOR_T FloatTensor  // or ByteTensor (uint8)
#define Env Target
#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) { env->num_agents = 8; ... init(env); }
void my_log(Log* log, Dict* out) { dict_set(out, "score", log->score); ... }
```

Optional opt-in macros (all in `src/vecenv.h`): `MY_ACTION_MASK <size>`, `MY_VEC_INIT`/`MY_VEC_CLOSE` (custom batch construction), `MY_USES_PERM` (agent-slot permutation for self-play team swaps), `MY_USES_TAGS` (per-env tags for the self-play pool), `my_shared`/`my_get`/`my_put` (shared state).

**Workflow:** copy `ocean/squared` (single-agent) or `ocean/target` (multi-agent) → write ini → `./build.sh myenv` (or `puffer build`) → `puffer train myenv`. Dev/debug: `./build.sh myenv --local` (sanitizers) or `--fast` (standalone desktop binary from `myenv.c`).

## 2. Multi-agent & self-play

- Vectorization is **agent-major**: config sets `[vec] total_agents` (e.g. chess: 8192); `my_vec_init` creates envs until agent slots are filled (2 agents/env for chess self-play → 4096 boards in one process). Each agent slot owns one row in obs/actions/rewards/terminals.
- **Turn-based pattern (chess, `ocean/chess/chess.h`, 3167 lines):** both players are live agent slots every tick. `c_step` applies only the side-to-move's action. The waiting player's action mask permits only a "pass" action. Chess further decomposes moves into **pick-square then place-square phases** (2 ticks/ply, `ACT_SIZES {97}`, masked each phase) — the canonical way to avoid a giant joint action space.
- **`pufferlib/selfplay.py`** is a full self-play pool system integrated with the trainer (`[selfplay] enabled = 1`): disk-backed opponent pool, ELO tracking, snapshotting, winrate-gated opponent swaps, stride-eviction for temporal diversity, up to 8 concurrent frozen banks. Requires `MY_USES_TAGS`/`MY_USES_PERM` plus `int tag; int boundary_reached;` fields on the env struct. `puffer match` evaluates two checkpoints head-to-head.
- The trainer expects synchronous lockstep: every agent slot gets an action and produces obs/reward/terminal every tick.

## 3. Action spaces & masking

- **MultiDiscrete: yes, natively.** `NUM_ATNS` heads with per-head sizes `ACT_SIZES {…}` (moba: `{7, 7, 3, 2, 2, 2}`). Continuous also supported.
- **Invalid-action masking: first-class in the native trainer.** `#define MY_ACTION_MASK 97` (chess) allocates a per-agent `unsigned char` mask buffer, env writes it each step, it's async-copied to GPU alongside obs, and `src/pufferlib.cu` applies it in **sampling, log-prob, and entropy** (`masked_logit`), and stores it in the rollout buffer for the learner. The mask covers the concatenated logits of all heads.
- Caveats: only `chess` uses it so far; the **PyTorch fallback backend has no masking** — masking commits you to the native (CUDA) backend.

## 4. Observation conventions

- **Flat 1-D per-agent arrays only** — `OBS_SIZE` elements, no dict/structured spaces in the native path. Dtype via `OBS_TENSOR_T`: `FloatTensor` or `ByteTensor` (uint8 — used by chess, moba, nmmo3). Keep values roughly in [-1, 1] (bytes are normalized).
- 2-D grids are flattened: egocentric crop (snake: 11×11 window) or full-board planes (chess: 167 bytes = board + phase/selection features).
- **Native default policy** (`src/models.cu`): `flat obs → Encoder (linear) → N× MinGRU + highway layers → Decoder → per-head logits + value`. **No CNN in the native trainer** — MinGRU recurrence is the memory mechanism (good for fog-of-war). Custom CUDA encoders are the escape hatch. The torch backend does have CNN encoders + LSTM/GRU/MinGRU.

## 5. Performance & vectorization

- Claims: native backend **~20M steps/sec training**; torch backend up to 5M; torch CPU on Mac <200k sps.
- **Vectorization is single-process, in-memory** — no Python multiprocessing. Agents chunked into `num_buffers` buffers; each buffer has a pthread manager bound to its own CUDA stream; within a buffer, envs step via OpenMP. GPU inference and CPU sim overlap via double-buffering.
- Guidance for millions of SPS: zero allocation in `c_step`, `rand_r(&env->rng)` per-env RNG, keep `OBS_SIZE` small (chess deliberately shrank 1082→167). Compiled `-O2 -mavx2 -mfma`.
- **Hardware:** the native backend **requires CUDA** (multi-GPU via NCCL built in). CPU-only: `./build.sh env --cpu` + torch backend.

## 6. Emscripten / web — one C core serves both

From `build.sh` (`--web` mode):

```sh
emcc -o "build/web/$ENV/game.html" "$SRC_DIR/$ENV.c" -O3 \
    raylib-5.5_webassembly/lib/libraylib.a \
    -sUSE_GLFW=3 -sUSE_WEBGL2=1 -sASYNCIFY -sFILESYSTEM -sFORCE_FILESYSTEM=1 \
    --shell-file vendor/minshell.html \
    -sINITIAL_MEMORY=512MB -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=512KB \
    -DNDEBUG -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES3 \
    --preload-file resources/$ENV@resources/$ENV
```

The same `env.h` core powers both. The `.c` demo runs the **trained RL policy in pure C** via `src/puffernet.h` (~1100 lines): an arena-allocated MinGRU+highway forward pass that loads flattened PyTorch weights from a `.bin` file:

```c
Weights* weights = load_weights("resources/target/target_weights.bin");
PufferNet* net = make_puffernet(weights, 8, 28, 128, 4, logit_sizes, 2);
while (!WindowShouldClose()) {
    forward_puffernet(net, env.observations, env.actions);
    c_step(&env); c_render(&env);
}
```

So: **write the game core once in C → it is simultaneously the RL env (binding.c), a desktop game (`--fast`), and a browser game with a WASM-embedded RL opponent (`--web`)**. Rendering is raylib 5.5 (WebGL2); for a custom web UI, skip raylib and expose the core to JS instead.

## 7. Existing strategy envs to crib from

| Env | Status | Why it matters |
|---|---|---|
| **`ocean/chess`** | **The gold standard — crib this** | Turn-based, 2-agent self-play, `MY_ACTION_MASK`, pick/place phased actions, `MY_USES_PERM`/`MY_USES_TAGS`, curriculum, tuned selfplay ini, trained ~1e12 steps |
| `ocean/moba` | ported | 5v5 team multi-agent, MultiDiscrete, byte obs, custom `my_vec_init`, puffernet web demo |
| `ocean/go`, `hex`, `connect4`, `tripletriad` | ported | Board-game turn-based patterns without masking |
| `ocean/nmmo3`, `terraform`, `snake` | ported | Large grid worlds, egocentric byte obs |
| `ocean/tactical` | legacy, unported | Turn-based tactics; uses removed 3.0 binding — ideas only |

## Recommended integration for Polytopia

1. **Core:** one `polytopia.h` with a POD `Env` struct (map arrays fixed at max size, unit pools, per-player econ), zero allocations in `c_step`, `rand_r` RNG.
2. **Multi-agent:** `num_agents = num_players` per env; all players live agent slots; only the active player's action applied per tick, others mask-forced to "pass" (chess pattern). Add tags/perm support for the self-play pool → league-style self-play for free.
3. **Actions:** decompose turns into micro-decisions across ticks (select actor/tile → action type → target), small masked heads per tick. Strongly preferred over a giant joint per-turn space; matches how chess was actually trained.
4. **Obs:** `ByteTensor`, flat planes of the (fog-masked) map from the acting player's perspective + scalars; rely on MinGRU memory rather than history stacking.
5. **Training:** `config/polytopia.ini` starting from `config/chess.ini`; `./build.sh polytopia && puffer train polytopia` on a CUDA box; `puffer match` for evals; `puffer sweep` (Protein) for hypers.
6. **Web:** export weights to `.bin`, embed via `puffernet.h`, or custom TS UI over an exposed C ABI.
7. **Risks:** native trainer is CUDA-only, bf16 by default; API is young/brittle in spots (one env per `_C.so` build); no CNN in native policy; torch fallback lacks action masking.

**Key files to read:** `src/vecenv.h` (binding API — read first), `src/pufferlib.cu` (trainer, masking), `src/puffernet.h` (C inference), `src/models.cu` + `pufferlib/models.py`, `pufferlib/selfplay.py`, `pufferlib/pufferl.py`, `build.sh`, `ocean/chess/{binding.c,chess.h}`, `ocean/target/*`, `config/{default,chess}.ini`.
