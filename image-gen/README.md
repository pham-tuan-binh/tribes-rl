# image-gen — realistic asset pack pipeline

Regenerates the game's sprites in a realistic, Age-of-Empires-style look using
OpenAI `gpt-image-1` (reference-guided from the existing assets) + local
background removal (`rembg`). See [`PLAN.md`](PLAN.md) for the full design and
prompt hierarchy.

## Setup

Uses [uv](https://docs.astral.sh/uv/). It creates the env and installs deps on
first `uv run` — no manual venv step.

```bash
cd image-gen
uv sync                       # create env + install deps (optional; uv run does it too)
cp .env.example .env          # put your OPENAI_API_KEY in it
```

That API key is the only thing you configure.

Image models render sprites on an opaque background, so **background removal is
a built-in step**: every cut-out sprite goes through a local `rembg` pass
(installed by `uv sync`, no per-image cost). Terrain tiles and soft overlays
(fog/shine) skip it automatically. Control it with `--bg always|auto|never`.

## Sample a few and eyeball them

```bash
uv run generate.py --dry-run                 # print the composed prompts, $0
uv run generate.py --only warrior farm fish  # generate 3 assets
open out/sprites                             # review the results
```

Nothing touches `web/assets/` until you explicitly install:

```bash
uv run generate.py --install                 # copy out/sprites/ -> web/assets/
```

Then reload the web client (`web/serve.py`) to see them in-game.

## Common runs

```bash
uv run generate.py --group building          # a whole category
uv run generate.py --group unit              # all units (+ auto team-recolor)
uv run generate.py --only knight --fidelity high   # hug the original silhouette
uv run generate.py --limit 5                 # first 5 of the default set
uv run generate.py --group terrain           # phase-2, seamless tiles (see PLAN)
```

## Providers — OpenAI or OpenRouter

Default is OpenAI `gpt-image-1` (direct). You can also route through the
OpenRouter Unified Image API with one key — its default model is
`openai/gpt-image-2` (OpenAI's image model via OpenRouter), and you can switch
to Gemini / Seedream / FLUX with `--model`:

```bash
# OpenAI direct (default):
uv run generate.py --only warrior

# OpenRouter, default model openai/gpt-image-2:
uv run generate.py --provider openrouter --only warrior

# any other OpenRouter image model:
uv run generate.py --provider openrouter \
    --model bytedance-seed/seedream-4.5 --only warrior
```

Background removal runs regardless of provider/model, so the choice is about
art style and cost, not transparency. Reference-guided editing (the existing
asset as a base) works on both — OpenRouter via `input_references`.

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
