# image-gen — realistic asset pack pipeline

Regenerates the game's sprites in a realistic, Age-of-Empires-style look using
OpenAI `gpt-image-1` (reference-guided from the existing assets) + local
background removal (`rembg`). See [`PLAN.md`](PLAN.md) for the full design and
prompt hierarchy.

## Setup

```bash
cd image-gen
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env          # put your OPENAI_API_KEY in it
```

That API key is the only thing you configure.

## Sample a few and eyeball them

```bash
python generate.py --dry-run                 # print the composed prompts, $0
python generate.py --only warrior farm fish  # generate 3 assets
open out/sprites                             # review the results
```

Nothing touches `web/assets/` until you explicitly install:

```bash
python generate.py --install                 # copy out/sprites/ -> web/assets/
```

Then reload the web client (`web/serve.py`) to see them in-game.

## Common runs

```bash
python generate.py --group building          # a whole category
python generate.py --group unit              # all units (+ auto team-recolor)
python generate.py --only knight --fidelity high   # hug the original silhouette
python generate.py --limit 5                 # first 5 of the default set
python generate.py --group terrain           # phase-2, seamless tiles (see PLAN)
```

## What you get

- `out/masters/<id>.png` — raw 1024px generations (keep as hi-res masters).
- `out/sprites/<same path as web/assets>` — finished, cut-out, resized sprites.
- Units are generated **once** as a neutral master, then auto-recolored into
  `0..3.png` + `0..3Exhausted.png` per player team color.

## Flags

| flag | meaning |
|---|---|
| `--group` | categories: `unit building resource action overlay terrain` |
| `--only`  | explicit asset ids (see `prompts/manifest.py`) |
| `--limit` | cap count (sampling) |
| `--size`  | `1024x1024` (default) / `1024x1536` / `1536x1024` |
| `--fidelity` | `low` (new art, loose) / `high` (hug original) |
| `--bg`    | rembg pass: `auto` / `always` / `never` |
| `--no-ref` | pure text-to-image, ignore reference assets |
| `--dry-run` | print prompts only, no API calls |
| `--install` | copy `out/sprites/` into `web/assets/` |
