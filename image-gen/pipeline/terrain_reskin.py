"""Terrain re-skinning — re-surface every tile in a family from one new texture.

Terrain is ~80 files across 7 families (plain, water, deepwater, mountain3,
forest2, village2, city3). Each is a diamond tile; edge variants bake a dark 3D
"skirt" on the down/left edges (coastlines / board edge). They must tile
seamlessly in the -45 deg render frame, so we can't just generate each one.

Instead: generate ONE new seamless top-down texture per family, then for every
existing tile in that family transfer the new texture through the original's
shape and lighting:

    result.rgb = new_texture * (original_luminance / mean_luminance)
    result.a   = original_alpha

The top face (near-uniform luminance) becomes the new texture; the darker skirt
pixels stay dark, so they still read as shaded cliffs; the alpha/silhouette is
untouched, so geometry and tiling are preserved pixel-for-pixel. Every tile in
a family samples the same resized texture, so identical tiles keep tiling.
"""
from __future__ import annotations

import io
from pathlib import Path

import numpy as np
from PIL import Image


def family_of(filename: str) -> str:
    """Family prefix of a terrain file: 'plain-down-left.png' -> 'plain'."""
    return Path(filename).stem.split("-")[0]


def _reskin_tile(orig: Image.Image, texture: Image.Image,
                 strength: float = 0.7) -> Image.Image:
    """Paint `texture` through `orig`'s alpha + luminance. Returns orig-sized RGBA.

    `strength` scales how much of the original's light/dark structure carries
    over: 1.0 keeps it fully (faceting from the old flat tiles bleeds through),
    lower values flatten the near-uniform top face while still darkening skirts.
    """
    w, h = orig.size
    tex = np.asarray(texture.convert("RGB").resize((w, h), Image.LANCZOS), np.float32)
    o = np.asarray(orig.convert("RGBA"), np.float32)
    alpha = o[..., 3]
    lum = 0.299 * o[..., 0] + 0.587 * o[..., 1] + 0.114 * o[..., 2]
    opaque = alpha > 8
    mean = lum[opaque].mean() if opaque.any() else 1.0
    rel = lum / max(mean, 1.0)                    # relative brightness (1 = average)
    rel = 1.0 + (rel - 1.0) * strength            # pull toward 1 to flatten facets
    rel = np.clip(rel, 0.0, 1.6)[..., None]
    out = np.empty_like(o)
    out[..., :3] = np.clip(tex * rel, 0, 255)
    out[..., 3] = alpha
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def reskin_family(texture_png: bytes, family: str, game_terrain: Path,
                  out_terrain: Path) -> list[Path]:
    """Re-skin the base tile + every edge variant of `family`. Returns written paths.

    `family` is the base prefix (plain, water, deepwater, mountain3, village2,
    city3, forest2) — matches `family.png` and `family-<variant>.png`.
    """
    texture = Image.open(io.BytesIO(texture_png))
    out_terrain.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    for src in sorted(game_terrain.glob(f"{family}*.png")):
        if family_of(src.name) != family:      # avoid mountain3 vs mountain matches
            continue
        orig = Image.open(src)
        _reskin_tile(orig, texture).save(out_terrain / src.name)
        written.append(out_terrain / src.name)
    return written
