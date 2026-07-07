# tribes-rl

An open-source reimplementation of *The Battle of Polytopia*, built for reinforcement learning. The game rules are ported from [GAIGResearch/Tribes](https://github.com/GAIGResearch/Tribes), the academic Java framework for Polytopia, and the port is verified move-for-move against it with golden traces. Full credit to the Tribes authors: without their implementation this project would not exist.

One C engine, two targets:

1. a headless [PufferLib](https://puffer.ai/) environment for fast self-play training, and
2. the same engine compiled to WASM for a website where you can watch the trained agents play, or play against them, entirely in the browser.

The game you play on the website is bit-identical to the environment the agent trained on.

## The game

- Might mode only: you win by capturing every enemy capital. There is no score victory.
- 11x11 generated maps, 2 to 4 players, fog of war on.
- 200-turn cap. Hitting it is a draw for everyone.
- Regular tribes only, no special tribes.

## Layout

```
core/       the game engine (C11, header-only) + tests
puffer/     PufferLib binding, training config, train/match scripts
web/        the website: WASM bridge, renderer, in-browser inference
tools/      golden-trace harness against the Java Tribes
docs/       porting specs distilled from the Tribes source
image-gen/  sprite generation pipeline (see the web section)
```

## The RL environment

### Using it

The env is a PufferLib 4.0 native environment (pure C, no Python in the hot loop).

```sh
git clone https://github.com/PufferAI/PufferLib && cd PufferLib
pip install -e .
/path/to/tribes-rl/puffer/install_into_pufferlib.sh   # copies the env into ocean/polytopia
puffer train polytopia                                # config comes from puffer/polytopia.ini
```

Player count is a compile-time knob:

```sh
EXTRA_CFLAGS="-DENV_PLAYERS=4" puffer train polytopia
```

`puffer/train_lk.sh` wraps this for a remote box, and `puffer/match_lk.sh` plays two checkpoints against each other for gating.

### Action space

One masked `Discrete(197)` head. The env turns each game turn into a sequence of micro-decisions with a SELECT, VERB, TARGET phase machine:

- actions 0 to 120: pick a tile (select a unit or city, or pick a target)
- actions 121 to 196: pick a verb (end turn, research one of 24 techs, move, attack, build, spawn, level up, gather, forest ops, and so on)

The action mask contains exactly the legal choices for the current phase, so the policy never has to learn what is legal. Immediate verbs (research, spawn, level-up) resolve in one step; targeted verbs (move, attack, build) go through a TARGET phase where the mask lists valid tiles.

### Observation

A flat `uint8` tensor, always from the observing player's perspective (1 = mine, 2 = enemy), with fog applied: unseen tiles show only fog, and opponents' stars and tech are hidden.

- 10 tile planes of 121 tiles each: terrain, resource, building, roads, city, unit type, unit owner, unit hp, unit status, selection cursor
- globals: phase, pending verb, am-I-active, turn number, then one block per player (stars, score, cities, kills, tribe, 24 tech bits), self first, opponents in seat order

Sizes: 1272 bytes for 2 players, 1301 for 3, 1330 for 4.

### Reward

The terminal reward mirrors how Polytopia scores a match: winning fast is worth more.

- win by domination: `+(1 + speed_bonus * (1 - turns/200))`, and the losers get the negative
- draw at the turn cap: -0.5 for everyone
- small dense shaping, all configurable in `puffer/polytopia.ini`:
  - `reward_city` 0.05 per city taken (zero-sum with the victim)
  - `reward_capital` 0.3 per enemy capital taken or lost
  - `reward_kill` 0.01 per star of unit value traded (kills minus losses, so disbanding is not an escape hatch)
  - `reward_explore` 0.001 per tile revealed
  - `reward_prod` 0.01 per point of income growth
  - `turn_penalty` 0.003 per game turn, charged to all players, so stalling always loses value
  - `reward_waste` 0.02 per star burned in do-undo cycles like grow forest then clear it

Generic score shaping is off by default because Tribes score rewards temples and monuments, which is score farming, not domination.

### Performance on an RTX 5090

With 4096 parallel agents and an 8.3M-param MinGRU policy (768 hidden, 4 layers), training runs at about 1.4M agent steps per second end to end, including learning. A 10B-step run finishes in about 2 hours. The env itself is plain C with no allocation in the step path, so throughput scales with cores.

### Correctness

The engine is verified against the Java Tribes with golden traces: a Java dumper replays seeds and dumps canonical state plus legal-action sets every step, and the C engine must match byte for byte. 80 seeds pass. `make test` runs the unit suites, `./tools/run_traces.sh` runs traces (clone the upstream first: `git clone --depth 1 https://github.com/GAIGResearch/Tribes reference/Tribes`). A `tribes_compat` flag keeps the Java-exact behavior for traces while the product rules make small fixes, for example roads only on plains.

## The website

One page, two modes:

- **Agents**: watch the trained agents play each other, with a speed slider up to unthrottled. The camera follows the active agent's fog of war, so you see the game the way that agent sees it.
- **Human**: you play against the agents, with the same fog.

No framework, no bundler, no npm. Plain ES-module JavaScript over the WASM engine. The trained checkpoint runs client-side with a small C inference lib (puffernet, vendored from PufferLib), so the site is fully static: no server, no API.

```sh
brew install emscripten     # once
./web/build.sh              # builds dist/poly2.js, poly3.js, poly4.js
python3 web/serve.py        # http://localhost:8080
```

Checkpoints go in `web/weights/` (`latest.bin` for 2 players, `latest_3p.bin`, `latest_4p.bin`). They are flat float32 exports of the trained policy. The bridge auto-detects the network size from the file, and samples with temperature 0.5 by default so spectated games look clean.

The site deploys to GitHub Pages automatically: `.github/workflows/pages.yml` builds the WASM in CI and publishes `web/` on every push to main. Set Pages > Source to "GitHub Actions" in the repo settings once.

### Assets

Sprites are generated by the `image-gen/` pipeline: reference-guided image generation plus local background removal, producing an original Age-of-Empires-style pack with four team colors per unit. Nothing touches the site until you install:

```sh
cd image-gen
uv run generate.py --only warrior farm    # generate a few, review in out/
uv run generate.py --install              # copy out/sprites/ -> web/assets/
```

See `image-gen/README.md` for the full pipeline.

## License

MIT. The game rules were ported from [GAIGResearch/Tribes](https://github.com/GAIGResearch/Tribes); this project contains none of its code, but it owes its correctness to it. *The Battle of Polytopia* is a trademark of Midjiwan AB, which is not affiliated with this project. All art in `web/assets/` is original, generated by the `image-gen/` pipeline.
