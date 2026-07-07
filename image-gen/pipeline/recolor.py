"""Team recoloring — one neutral unit master -> per-player sprites + Exhausted.

Units bake the player's color into the sprite (warrior/0.png is red = player 0,
etc.). Rather than generating 12 types x 4 colors x 2 states = 96 images, we
generate ONE neutral master per unit whose faction cloth is painted flat
magenta (#FF00FF, see prompts/style.py CATEGORY['unit']) and tint that region
to each team color here — guaranteeing palette consistency and cutting API cost
by ~10x.

For each master we emit, into the unit's game dir:
    0.png .. 3.png              tinted to PLAYER_COLORS
    0Exhausted.png .. 3Exhausted.png   desaturated+darkened variants

Detection: a pixel's "magenta-ness" = how much its red & blue exceed green.
The tint preserves the cloth's fold shading by scaling the team color by the
pixel's brightness, then alpha-blends by magenta-ness so edges stay clean.
"""
from __future__ import annotations

import io
from pathlib import Path

import numpy as np
from PIL import Image


def _hex(c: str) -> np.ndarray:
    c = c.lstrip("#")
    return np.array([int(c[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32)


def _tint(rgba: np.ndarray, team: np.ndarray) -> np.ndarray:
    """Replace magenta faction pixels with team color, keeping fold shading."""
    r, g, b = rgba[..., 0], rgba[..., 1], rgba[..., 2]
    # magenta-ness in [0,1]: red & blue both above green
    m = np.clip((np.minimum(r, b) - g) / 255.0, 0.0, 1.0)
    m = np.clip(m * 1.6, 0.0, 1.0)  # sharpen the mask a touch
    shade = (np.maximum.reduce([r, g, b]) / 255.0)[..., None]  # cloth brightness
    tinted = team[None, None, :] * shade
    out = rgba.copy()
    out[..., :3] = rgba[..., :3] * (1 - m[..., None]) + tinted * m[..., None]
    return out


def _exhaust(rgba: np.ndarray) -> np.ndarray:
    """Desaturate + darken to signal a spent/exhausted unit."""
    lum = (0.299 * rgba[..., 0] + 0.587 * rgba[..., 1] + 0.114 * rgba[..., 2])[..., None]
    out = rgba.copy()
    out[..., :3] = (rgba[..., :3] * 0.30 + lum * 0.70) * 0.82
    return out


def expand(master_png: bytes, out_dir: Path, colors: dict[int, str]) -> list[Path]:
    """Write 0..N.png and 0..NExhausted.png into out_dir. Returns written paths."""
    out_dir.mkdir(parents=True, exist_ok=True)
    base = np.asarray(Image.open(io.BytesIO(master_png)).convert("RGBA"), dtype=np.float32)
    written: list[Path] = []
    for idx, hexc in colors.items():
        team = _hex(hexc)
        tinted = _tint(base, team)
        exhausted = _exhaust(tinted)
        for arr, name in ((tinted, f"{idx}.png"), (exhausted, f"{idx}Exhausted.png")):
            img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
            path = out_dir / name
            img.save(path)
            written.append(path)
    return written
