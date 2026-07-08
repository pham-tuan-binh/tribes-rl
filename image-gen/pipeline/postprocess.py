"""Post-generation image ops: trim, center, resize to game target sizes.

Masters are kept at 1024px in out/masters/. The finished game sprites are
trimmed to their alpha bounds, re-centered with a small margin, and downscaled
with Lanczos to the size the renderer actually draws (see render.js: cells are
64px, buildings ~128, resources ~500 in source but drawn at 0.75*cell).

We keep finished sprites generous (256px) — the canvas renderer downscales at
draw time, and hi-dpi displays benefit from the headroom.
"""
from __future__ import annotations

import io

from PIL import Image

# Finished-sprite longest edge per category (px). Renderer downsamples further.
TARGET = {
    "unit": 256,
    "building": 256,
    "resource": 256,
    "action": 164,
    "overlay": 256,
    "terrain": 512,   # terrain must stay square & full-bleed (no trim)
    "tile": 256,      # standalone full-bleed tile (fog) — no trim
}


def _load(png_bytes: bytes) -> Image.Image:
    return Image.open(io.BytesIO(png_bytes)).convert("RGBA")


def _to_bytes(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def robust_bbox(img: Image.Image, thresh: int = 24, min_run: float = 0.004):
    """Alpha bounding box that ignores faint matte + stray speckles.

    A row/column counts as content only if its solid-ish pixels (alpha>thresh)
    exceed min_run of the image dimension — so a couple of orphan rembg specks
    far from the subject can't inflate the box (which is what leaves sprites
    off-center after naive getbbox()).
    """
    import numpy as np
    a = np.asarray(img.getchannel("A"))
    solid = a > thresh
    h, w = solid.shape
    rows = np.where(solid.sum(axis=1) > max(1, int(w * min_run)))[0]
    cols = np.where(solid.sum(axis=0) > max(1, int(h * min_run)))[0]
    if not len(rows) or not len(cols):
        return None
    return (int(cols[0]), int(rows[0]), int(cols[-1]) + 1, int(rows[-1]) + 1)


def trim_center(img: Image.Image, margin_frac: float = 0.06) -> Image.Image:
    """Crop to the robust alpha bbox, then pad to a square with an even margin."""
    bbox = robust_bbox(img)
    if bbox:
        img = img.crop(bbox)
    w, h = img.size
    side = int(max(w, h) * (1 + 2 * margin_frac))
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(img, ((side - w) // 2, (side - h) // 2), img)
    return canvas


def finish(png_bytes: bytes, category: str) -> bytes:
    """Full finishing pass for a sprite. Terrain is left full-bleed."""
    img = _load(png_bytes)
    size = TARGET.get(category, 256)
    if category in ("terrain", "tile"):   # full-bleed, no trim
        img = img.resize((size, size), Image.LANCZOS)
        return _to_bytes(img.convert("RGBA"))
    img = trim_center(img)
    img = img.resize((size, size), Image.LANCZOS)
    return _to_bytes(img)
