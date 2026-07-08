#!/usr/bin/env python3
"""Asset generation pipeline — compose prompt -> gpt-image-1 -> cutout -> finish.

Quick start (see README.md):
    uv sync                 # or let `uv run` do it on first call
    cp .env.example .env    # put your OPENAI_API_KEY in it
    uv run generate.py --dry-run                 # print prompts, spend nothing
    uv run generate.py --only warrior knight     # gen a couple to eyeball
    uv run generate.py --group building          # a whole category
    uv run generate.py --install                 # copy finished sprites into web/assets

Per asset the flow is:
    manifest subject + style layers  ->  full prompt
    gpt-image-1 (reference = existing asset, transparent bg)  ->  master PNG (1024)
    rembg cleanup pass  ->  transparent cutout
    trim / center / resize  ->  finished sprite in out/sprites/
    (units only) recolor master -> 0..3 + Exhausted into out/sprites/unit/<id>/

Nothing is written into web/assets until you pass --install, so you can review
out/sprites/ first.
"""
from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path

# repo layout: image-gen/ lives next to web/
HERE = Path(__file__).resolve().parent
REPO = HERE.parent
GAME_ASSETS = REPO / "web" / "assets"
MASTERS = HERE / "out" / "masters"
SPRITES = HERE / "out" / "sprites"

sys.path.insert(0, str(HERE))
from prompts import manifest, style  # noqa: E402


def _load_env():
    """Minimal .env loader so you don't need python-dotenv."""
    env = HERE / ".env"
    if not env.exists():
        return
    import os
    for line in env.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        os.environ.setdefault(k.strip(), v.strip().strip('"').strip("'"))


def terrain_texture(family: str) -> Path | None:
    """Freshly generated texture for a terrain family, if it exists yet.

    Prefers this run's output (out/sprites/terrain/<family>.png — written when
    the terrain asset is generated, which is why terrain runs FIRST), falling
    back to the installed game tile.
    """
    for p in (SPRITES / "terrain" / f"{family}.png",
              GAME_ASSETS / "terrain" / f"{family}.png"):
        if p.exists():
            return p
    return None


def gen_one(asset, args) -> Path | None:
    """Generate + process a single asset. Returns the finished sprite path."""
    prompt = style.compose(asset.category, asset.subject)

    ref = GAME_ASSETS / asset.ref if asset.ref else None
    use_ref = ref and ref.exists() and not args.no_ref
    style_ref = Path(args.style_ref).expanduser() if args.style_ref else None
    if style_ref and not style_ref.exists():
        raise FileNotFoundError(f"--style-ref not found: {style_ref}")

    # Ordered reference images + what each one means (explained to the model).
    refs, roles = [], []
    if use_ref:
        refs.append(ref)
        roles.append("the ORIGINAL game asset — match its subject, silhouette and scale")
    ctx = terrain_texture(asset.terrain_ctx) if asset.terrain_ctx else None
    if ctx is not None:
        refs.append(ctx)
        if asset.category == "terrain":
            roles.append("the game's base ground tile — stay in the same palette "
                         "temperature, saturation and lighting so adjacent tiles blend")
        else:
            roles.append("the GROUND TEXTURE this object stands on in-game — match "
                         "its palette and lighting so the object blends in when "
                         "placed on it, with a small soft contact shadow at the base")
    if style_ref:
        refs.append(style_ref)
        roles.append("the target ART STYLE — reproduce its rendering style, color, "
                     "materials and lighting exactly")
    if roles:
        listing = "\n".join(f"Reference image {i + 1}: {r}." for i, r in enumerate(roles))
        prompt += f"\n\n{len(roles)} reference image(s) are attached, in order:\n{listing}"

    if args.dry_run:
        print(f"\n=== {asset.id} [{asset.category}]  ref={asset.ref}")
        print(prompt)
        return None

    from pipeline import bg_remove, postprocess, providers

    tag = "edit(ref)" if use_ref else "generate"
    if ctx is not None:
        tag += f"+{asset.terrain_ctx}"
    if style_ref:
        tag += "+style"
    print(f"[gen] {asset.id:14s} {args.provider}:{args.model or providers.DEFAULT_MODEL[args.provider]} "
          f"{tag} ...", flush=True)

    or_ignore = ([s.strip() for s in args.or_ignore.split(",") if s.strip()]
                 if args.or_ignore is not None else None)
    raw = providers.render(prompt, refs,
                           provider=args.provider, model=args.model,
                           size=args.size, fidelity=args.fidelity, or_ignore=or_ignore)

    MASTERS.mkdir(parents=True, exist_ok=True)
    master_path = MASTERS / f"{asset.id}.png"
    master_path.write_bytes(raw)

    # Background removal — mandatory for cut-out sprites, skipped for full-bleed
    # terrain and soft translucent overlays (fog/shine).
    if manifest.wants_cutout(asset):
        cut = bg_remove.cutout(raw, mode=args.bg)
    else:
        cut = raw
    finished = postprocess.finish(cut, asset.category)

    out_path = SPRITES / asset.out
    out_path.parent.mkdir(parents=True, exist_ok=True)

    if asset.category == "terrain":
        # Re-skin the whole family (base + all edge variants) from this one new
        # seamless texture, preserving each tile's shape/skirt so tiling holds.
        from pipeline import terrain_reskin
        family = Path(asset.out).stem
        paths = terrain_reskin.reskin_family(finished, family,
                                             GAME_ASSETS / "terrain", SPRITES / "terrain")
        print(f"      reskinned {len(paths)} '{family}' tiles -> "
              f"{(SPRITES / 'terrain').relative_to(HERE)}/")
    elif asset.recolor:
        # Don't write the neutral master into the game tree — the renderer only
        # loads the numbered team variants. Master is kept in out/masters/.
        from pipeline import recolor
        unit_dir = out_path.parent
        recolor.expand(finished, unit_dir, manifest.PLAYER_COLORS)
        print(f"      recolored -> {unit_dir.relative_to(HERE)}/{{0..3}}{{,Exhausted}}.png")
    else:
        out_path.write_bytes(finished)
        print(f"      -> {out_path.relative_to(HERE)}")
    return out_path


def recenter():
    """Post-processing loop: re-trim/center every cut-out sprite in out/sprites.

    Fixes sprites that came out off-center (faint rembg speckles used to inflate
    the naive alpha bbox). Terrain / fog tiles are full-bleed and skipped.
    """
    import io
    from PIL import Image
    from pipeline import postprocess

    skip_prefix = ("terrain/",)   # walls lives here but is a sprite — handled below
    fixed = 0
    for src in sorted(SPRITES.rglob("*.png")):
        rel = src.relative_to(SPRITES).as_posix()
        if rel == "fog.png" or (rel.startswith(skip_prefix) and "walls" not in rel):
            continue
        img = Image.open(src).convert("RGBA")
        size = img.size[0]
        out = postprocess.trim_center(img).resize((size, size), Image.LANCZOS)
        buf = io.BytesIO()
        out.save(buf, format="PNG")
        src.write_bytes(buf.getvalue())
        fixed += 1
    print(f"re-centered {fixed} sprites in {SPRITES.relative_to(HERE)}/")
    print("now run:  uv run generate.py --install")


def install():
    """Copy everything under out/sprites/ into web/assets/ (mirrors layout)."""
    if not SPRITES.exists():
        sys.exit("nothing in out/sprites/ — generate first")
    n = 0
    for src in SPRITES.rglob("*.png"):
        rel = src.relative_to(SPRITES)
        dst = GAME_ASSETS / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        n += 1
    print(f"installed {n} files into {GAME_ASSETS.relative_to(REPO)}/")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--group", nargs="*", choices=list(manifest.GROUPS),
                   help="asset categories to run (default: phase-1 sprite groups)")
    p.add_argument("--only", nargs="*", metavar="ID",
                   help="explicit asset ids, e.g. --only warrior farm attack")
    p.add_argument("--limit", type=int, help="cap number of assets (sampling)")
    p.add_argument("--jobs", type=int, default=4,
                   help="parallel generations within a stage (default 4; 1 = sequential)")
    p.add_argument("--provider", default="openai", choices=["openai", "openrouter"],
                   help="image backend (openai=gpt-image-1, openrouter=Unified Image API)")
    p.add_argument("--model", default=None,
                   help="model override, e.g. google/gemini-3.1-flash-image or "
                        "bytedance-seed/seedream-4.5 (openrouter); default per provider")
    p.add_argument("--size", default="1024x1024",
                   choices=["1024x1024", "1024x1536", "1536x1024"])
    p.add_argument("--fidelity", default="low", choices=["low", "high"],
                   help="input_fidelity for reference edits (high hugs the original)")
    p.add_argument("--bg", default="always", choices=["auto", "always", "never"],
                   help="rembg background-removal pass (default always; models "
                        "don't emit transparency). auto=only if border looks clean")
    p.add_argument("--or-ignore", default="google-ai-studio",
                   help="comma-separated OpenRouter upstream providers to exclude "
                        "(default google-ai-studio, which geo-blocks some regions). "
                        "Pass '' to allow all.")
    p.add_argument("--no-ref", action="store_true",
                   help="ignore the per-asset reference image, pure text-to-image")
    p.add_argument("--style-ref", metavar="PATH",
                   help="shared art-style reference image (e.g. ~/Downloads/aoe.jpg) "
                        "passed alongside every asset so the pack matches one look")
    p.add_argument("--dry-run", action="store_true",
                   help="print composed prompts and exit (no API calls, no cost)")
    p.add_argument("--install", action="store_true",
                   help="copy out/sprites/ into web/assets/ and exit")
    p.add_argument("--recenter", action="store_true",
                   help="post-process loop: re-trim/center all cut-out sprites "
                        "in out/sprites/ (no API calls), then exit")
    args = p.parse_args()

    if args.install:
        install()
        return
    if args.recenter:
        recenter()
        return

    _load_env()
    groups = args.group if (args.group or args.only) else manifest.DEFAULT_GROUPS
    assets = manifest.select(groups=groups, ids=args.only)
    if args.limit:
        assets = assets[:args.limit]
    if not assets:
        sys.exit("no assets matched your filters")
    # Pipeline order: plain terrain first (everything references its palette),
    # then the other terrain families, then the objects that stand on them.
    assets.sort(key=lambda a: 0 if a.id == "plain" else 1 if a.category == "terrain" else 2)

    print(f"{'DRY RUN — ' if args.dry_run else ''}{len(assets)} asset(s): "
          f"{', '.join(a.id for a in assets)}")

    # Stages honor the dependency chain: objects reference terrain textures
    # generated earlier in the SAME run, so terrain must land first.
    #   stage 0: plain (every other terrain family references its palette)
    #   stage 1: remaining terrain families   (parallel)
    #   stage 2: everything that stands on it (parallel)
    stages = [
        [a for a in assets if a.id == "plain"],
        [a for a in assets if a.category == "terrain" and a.id != "plain"],
        [a for a in assets if a.category != "terrain"],
    ]

    ok, failed = 0, []

    def run_one(a):
        try:
            gen_one(a, args)
            return a, None
        except Exception as e:  # keep going; one bad asset shouldn't kill a batch
            return a, e

    from concurrent.futures import ThreadPoolExecutor
    for stage in stages:
        if not stage:
            continue
        if args.dry_run or args.jobs <= 1 or len(stage) == 1:
            results = [run_one(a) for a in stage]
        else:
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                results = list(pool.map(run_one, stage))
        for a, err in results:
            if err is None:
                ok += 1
            else:
                failed.append(a.id)
                print(f"  !! {a.id} failed: {err}", file=sys.stderr)

    if not args.dry_run:
        print(f"\ndone: {ok}/{len(assets)} generated -> {SPRITES.relative_to(HERE)}/")
        if failed:
            print(f"retry failures with:  uv run generate.py --provider {args.provider} "
                  f"--only {' '.join(failed)}")
        print("review them, then:  uv run generate.py --install")


if __name__ == "__main__":
    main()
