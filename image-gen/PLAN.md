# Asset Regeneration Plan — realistic "Age of Empires" pack

Replace the game's current mixed-provenance, cartoony sprites (244 PNGs under
`web/assets/`, several flagged for unclear licensing in `web/assets/LICENSES.md`)
with an original, coherent, **realistic Age-of-Empires-style** art pack that we
own outright.

The pipeline is built (`generate.py` + `pipeline/` + `prompts/`). You add an
OpenAI API key and run it. This document explains the *why* behind the design
and the rollout.

---

## 1. Goals & constraints

- **One coherent look.** Every asset flows from a single style definition so the
  pack reads as one hand. Realistic, painterly, muted/earthy, upper-left light —
  AoE II: Definitive Edition / AoE IV territory. Not cartoon, not pixel art.
- **Drop-in compatible.** Output must satisfy the renderer as-is: same file
  paths, transparent PNGs for sprites, per-player numbered unit files
  (`0..3.png`, `0..3Exhausted.png`), square full-bleed terrain tiles. No engine
  changes required. (Verified against `web/js/assets.js` + `web/js/render.js`.)
- **License-clean.** Original generations we can redistribute — clears the three
  flagged assets (`city*`, `whale2`, `ore2`).
- **Cheap to iterate & samplable.** Generate one or a handful, eyeball, re-run.
  Nothing overwrites the live assets until `--install`.
- **Minimal config.** The only knob the user sets is `OPENAI_API_KEY`.

---

## 2. Best-approach decisions (and why)

| Decision | Choice | Why |
|---|---|---|
| **Image model** | OpenAI `gpt-image-1` | Native transparent-background output, reference-image editing, strong prompt adherence, and `input_fidelity` control. |
| **Reference-guided vs. text-only** | `images.edit` with the *existing asset* as reference (default) | Carries over silhouette, scale and framing so new art reads as the same object at the same size. `input_fidelity=low` keeps it loose (new realistic art, not a copy); `--fidelity high` hugs the original. `--no-ref` for pure text. |
| **Background removal** | `gpt-image-1 background=transparent` first, **`rembg`** (local, free, U²-Net) as a cleanup pass | Model cutout is usually clean; rembg deterministically removes residual matte/halo with no per-image cost. `--bg auto` only runs it when the border isn't already clean. |
| **Per-player unit colors** | Generate **one neutral master** with a flat magenta faction region, then **recolor in code** to the 4 team colors + Exhausted | Turns 12×4×2 = 96 unit generations into **12**. Guarantees exact, consistent team palette; the magenta mask is trivially keyable. See §5. |
| **Output resolution** | Generate at 1024², keep as masters, downscale (Lanczos) to game sizes | Renderer draws at 64px cells (units/buildings ≤128, resources 0.75·cell). Hi-res masters give hi-dpi headroom and future-proofing. |
| **Terrain** | Deferred to **phase 2** with a re-skinning approach (§6) | The 91 terrain files are 7 bases × ~24 seamless edge-variants that must tile in the −45° diamond frame. Naive generation won't align. Sprites are the clean immediate win. |
| **Language/stack** | Python (openai + Pillow + numpy + rembg) | rembg is Python; everything is scriptable, no service to stand up. |

---

## 3. Asset inventory

| Category | Files today | What we generate | Notes |
|---|---|---|---|
| Units | 12 dirs (~120 files incl. color+exhausted) | **12 neutral masters** → recolored | phase 1 |
| Buildings | 10 | 10 | transparent, isometric — phase 1 |
| Resources | 7 | 7 | transparent world objects — phase 1 |
| Actions | 9 | 9 | HUD glyphs — phase 1 |
| Overlays | fog, shine, walls (+roads) | 3 | phase 1 (roads: see §6) |
| Terrain | 91 (7 base + edge variants) | 7 base + reskin variants | **phase 2** |

Phase-1 API generations: **~41**. Everything else is derived in code.

---

## 4. Prompt hierarchy

Defined in `prompts/style.py`; subjects in `prompts/manifest.py`. Four layers,
composed by `style.compose(category, subject)`:

```
L0  STYLE_CORE   ── the global look. Change this one string → whole pack shifts.
L1  CATEGORY[…]  ── per-class framing: view angle, composition, scale intent.
                    unit · building · resource · terrain · action · overlay
L2  <subject>    ── the one-line asset description (manifest.py), e.g.
                    "an archer drawing a longbow, leather bracer…, hood as the
                     faction-color region"
Lx  TECH_*       ── shared constraints, appended last:
                    TECH_SPRITE → transparent, single centered object, no text
                    TECH_TILE   → full-bleed, seamless edges (terrain)
```

Why a hierarchy: art direction lives in exactly one place (L0), each asset
class is tuned once (L1), and per-asset prompts stay to a single readable line
(L2). Retuning "the whole look" or "all buildings" is a one-edit change, and
consistency is structural rather than copy-pasted.

Two L1 conventions worth calling out:
- **Units** declare a *flat magenta (#FF00FF) faction region* (a sash, banner,
  sail, caparison…) that §5 recolors per team.
- **Terrain** uses `TECH_TILE` (full-bleed, seamless) instead of the sprite
  cut-out block.

`uv run generate.py --dry-run` prints every composed prompt for review before
spending anything.

---

## 5. Per-player recoloring (`pipeline/recolor.py`)

`render.js` bakes team color into each unit sprite and expects
`unit/<type>/{0..3}.png` + `{0..3}Exhausted.png` (tribe 0 red, 1 blue, 2 yellow,
3 green — from `PLAYER_COLOR`).

1. Generate one **neutral master** per unit; its faction cloth is flat magenta.
2. For each team color: detect magenta-ness per pixel (`min(R,B) − G`), scale
   the team color by the pixel's brightness (preserving cloth fold shading),
   and alpha-blend by magenta-ness so anti-aliased edges stay clean.
3. Derive **Exhausted** by desaturating + darkening the tinted variant.
4. Write `0..3.png` + `0..3Exhausted.png` into the unit dir.

Result: exact palette match to the HUD/borders, consistent across all 12 units,
and one generation instead of ninety-six.

---

## 6. Phase 2 — terrain (the hard part)

Terrain tiles must tile seamlessly and come in ~24 edge-variants per base that
align inside the isometric diamond. Two viable routes, in preference order:

- **Re-skin existing variant masks (recommended).** Generate one seamless
  base texture per terrain type (`--group terrain` already does the base). Then
  composite that texture through each existing variant PNG's **alpha/shape mask**
  so all 24 variants inherit the new look while keeping pixel-perfect geometry.
  This reuses the proven tiling geometry and only swaps the surface. (Builder
  script to add: `pipeline/terrain_reskin.py`.)
- **Full generation + manual seam fixing.** Higher fidelity, far more labor;
  only if re-skinning quality is insufficient.

Roads (`road-v-half`, `road-d-half`) are rotated/tiled fragments — treat like
terrain re-skinning, not free sprites.

---

## 7. Rollout

1. **Prove the look.** `--dry-run`, then generate 2–3 units + 2–3 buildings.
   Iterate on `STYLE_CORE` / category templates until the vibe is right.
2. **Phase 1 batch.** `--group unit building resource action overlay` (default).
   Review `out/sprites/`, `--install`, load the web client.
3. **Tune outliers.** Re-run individual ids with `--fidelity high` or edited
   subjects as needed.
4. **Phase 2 terrain.** Base textures → re-skin variants → validate tiling in
   the diamond view.
5. **Relicense.** Update `web/assets/LICENSES.md` / attribution once assets are
   original; retire the flagged files.

---

## 8. Cost & risk

- **Cost.** ~41 phase-1 generations at gpt-image-1 high-quality 1024² (order
  ~$0.15–0.20 each ⇒ roughly **$6–8** for a full phase-1 pass; sampling is
  cents). Re-rolls are the main variable. Terrain adds ~7 base generations.
- **Risks.**
  - *Style drift across assets* → mitigated by the single L0 definition + shared
    reference framing; review as a contact sheet.
  - *Faction-mask bleed* (model shades the magenta) → prompt forces flat magenta;
    recolor mask is robust to mild anti-aliasing; bump mask sharpen if needed.
  - *Transparent-bg halos* → `rembg` cleanup pass (`--bg always` if a batch is dirty).
  - *Terrain seams* → the re-skin route sidesteps generative seam alignment.

---

## 9. Files

```
image-gen/
  PLAN.md                 ← this document
  README.md               ← quickstart / flags
  pyproject.toml          ← uv project + deps (openai, pillow, numpy, rembg)
  .env.example            ← OPENAI_API_KEY (the only config)
  generate.py             ← CLI: compose → gpt-image-1 → cutout → finish → install
  prompts/
    style.py              ← L0 STYLE_CORE, L1 CATEGORY, Lx TECH  (+ compose)
    manifest.py           ← L2 subjects + wiring (ref/out/recolor) for every asset
  pipeline/
    openai_client.py      ← gpt-image-1 generate() / edit() wrapper
    bg_remove.py          ← rembg cleanup pass
    recolor.py            ← neutral master → team colors + Exhausted
    postprocess.py        ← trim / center / resize to game sizes
  out/                    ← generated (gitignored): masters/ + sprites/
```
