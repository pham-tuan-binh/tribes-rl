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
}


def _load(png_bytes: bytes) -> Image.Image:
    return Image.open(io.BytesIO(png_bytes)).convert("RGBA")


def _to_bytes(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def trim_center(img: Image.Image, margin_frac: float = 0.06) -> Image.Image:
    """Crop to alpha bounding box, then pad to a square with an even margin."""
    bbox = img.getchannel("A").getbbox()
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
    if category == "terrain":
        img = img.resize((size, size), Image.LANCZOS)
        return _to_bytes(img.convert("RGBA"))
    img = trim_center(img)
    img = img.resize((size, size), Image.LANCZOS)
    return _to_bytes(img)
