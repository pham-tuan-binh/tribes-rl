"""Background removal — the cleanup pass after generation.

gpt-image-1's native transparent background is usually clean, but can leave a
faint matte halo or fail on busy sprites. `rembg` (U^2-Net, runs locally, free,
no per-image cost) is our tool of choice for a deterministic second pass.

Strategy: if the image already has a mostly-transparent border we trust the
model's cutout and skip rembg; otherwise we run rembg. Force either way with
the `mode` argument.
"""
from __future__ import annotations

import io

from PIL import Image

_MODES = ("auto", "always", "never")


def _already_cut_out(img: Image.Image, thresh: int = 8) -> bool:
    """Heuristic: is the 1px border already transparent?"""
    if img.mode != "RGBA":
        return False
    a = img.getchannel("A")
    w, h = a.size
    px = a.load()
    edge = [px[x, 0] for x in range(w)] + [px[x, h - 1] for x in range(w)]
    edge += [px[0, y] for y in range(h)] + [px[w - 1, y] for y in range(h)]
    return (sum(edge) / len(edge)) < thresh


def _rembg(png_bytes: bytes) -> bytes | None:
    """Run rembg if the optional dep is installed; else None (graceful skip)."""
    try:
        from rembg import remove  # lazy: keep --dry-run / core install import-free
    except ImportError:
        print("      (rembg not installed — trusting gpt-image-1 transparency; "
              "`uv sync --extra rembg` to enable the cleanup pass)")
        return None
    return remove(png_bytes)


def cutout(png_bytes: bytes, mode: str = "auto") -> bytes:
    """Return PNG bytes with a transparent background.

    mode="auto"   run rembg only if the border isn't already transparent
    mode="always" always run rembg
    mode="never"  trust the model's transparency, no-op

    If rembg isn't installed, falls back to the model's own transparency.
    """
    if mode not in _MODES:
        raise ValueError(f"mode must be one of {_MODES}")
    if mode == "never":
        return png_bytes
    if mode == "auto":
        img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
        if _already_cut_out(img):
            return png_bytes
    return _rembg(png_bytes) or png_bytes
