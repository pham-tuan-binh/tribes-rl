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


def gen_one(asset, args) -> Path | None:
    """Generate + process a single asset. Returns the finished sprite path."""
    prompt = style.compose(asset.category, asset.subject)

    if args.dry_run:
        print(f"\n=== {asset.id} [{asset.category}]  ref={asset.ref}")
        print(prompt)
        return None

    from pipeline import bg_remove, openai_client, postprocess

    ref = GAME_ASSETS / asset.ref if asset.ref else None
    use_ref = ref and ref.exists() and not args.no_ref
    print(f"[gen] {asset.id:14s} {'edit(ref)' if use_ref else 'generate   '} ...", flush=True)

    if use_ref:
        raw = openai_client.edit(prompt, ref, size=args.size,
                                 input_fidelity=args.fidelity)
    else:
        raw = openai_client.generate(prompt, size=args.size)

    MASTERS.mkdir(parents=True, exist_ok=True)
    master_path = MASTERS / f"{asset.id}.png"
    master_path.write_bytes(raw)

    cut = bg_remove.cutout(raw, mode=args.bg)
    finished = postprocess.finish(cut, asset.category)

    out_path = SPRITES / asset.out
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(finished)

    if asset.recolor:
        from pipeline import recolor
        unit_dir = out_path.parent
        recolor.expand(finished, unit_dir, manifest.PLAYER_COLORS)
        print(f"      recolored -> {unit_dir.relative_to(HERE)}/{{0..3}}{{,Exhausted}}.png")

    print(f"      -> {out_path.relative_to(HERE)}")
    return out_path


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
    p.add_argument("--size", default="1024x1024",
                   choices=["1024x1024", "1024x1536", "1536x1024"])
    p.add_argument("--fidelity", default="low", choices=["low", "high"],
                   help="input_fidelity for reference edits (high hugs the original)")
    p.add_argument("--bg", default="auto", choices=["auto", "always", "never"],
                   help="rembg background-removal pass")
    p.add_argument("--no-ref", action="store_true",
                   help="ignore reference images, pure text-to-image")
    p.add_argument("--dry-run", action="store_true",
                   help="print composed prompts and exit (no API calls, no cost)")
    p.add_argument("--install", action="store_true",
                   help="copy out/sprites/ into web/assets/ and exit")
    args = p.parse_args()

    if args.install:
        install()
        return

    _load_env()
    groups = args.group if (args.group or args.only) else manifest.DEFAULT_GROUPS
    assets = manifest.select(groups=groups, ids=args.only)
    if args.limit:
        assets = assets[:args.limit]
    if not assets:
        sys.exit("no assets matched your filters")

    print(f"{'DRY RUN — ' if args.dry_run else ''}{len(assets)} asset(s): "
          f"{', '.join(a.id for a in assets)}")
    ok = 0
    for a in assets:
        try:
            gen_one(a, args)
            ok += 1
        except Exception as e:  # keep going; one bad asset shouldn't kill a batch
            print(f"  !! {a.id} failed: {e}", file=sys.stderr)
        if not args.dry_run:
            time.sleep(0.3)  # gentle on rate limits
    if not args.dry_run:
        print(f"\ndone: {ok}/{len(assets)} generated -> {SPRITES.relative_to(HERE)}/")
        print("review them, then:  uv run generate.py --install")


if __name__ == "__main__":
    main()
