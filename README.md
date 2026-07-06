# polytopia-rl

A from-scratch rebuild of *The Battle of Polytopia* (rules ported from
[GAIGResearch/Tribes](https://github.com/GAIGResearch/Tribes)) as:

1. a **headless C game engine** trained via [PufferLib](https://puffer.ai/) self-play, and
2. a **playable website** (TS + PixiJS over the same engine compiled to WASM),
   where the trained agent runs client-side.

One C core, two compile targets — the game you play in the browser is bit-identical
to the environment the agent trained on.

## Layout

- `docs/PLAN.md` — architecture, RL formulation, milestones
- `docs/spec-0*.md` — porting specs distilled from the Tribes source
- `core/` — the engine (`polytopia.h`, `constants.h`) + C tests
- `puffer/` — PufferLib binding + training config *(M2)*
- `web/` — TS + PixiJS frontend + WASM bridge *(M4)*
- `tools/` — Java golden-trace dumper *(M1)*
- `reference/Tribes/` — upstream Java implementation (git-ignored; clone it yourself:
  `git clone --depth 1 https://github.com/GAIGResearch/Tribes reference/Tribes`)

## Develop

```sh
make test    # build + run engine tests
make asan    # same, under address/UB sanitizers
```

Training runs on a CUDA box (PufferLib's native trainer requires it); the Mac-side
loop is engine tests and random-playout benchmarks only.
