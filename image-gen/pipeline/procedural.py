"""Procedural tiles — assets that need no AI (and no API credits).

fog of war is just an opaque murky shroud that the renderer draws instead of
terrain on unseen tiles. That's a perfect fit for procedural noise: free,
instant, and perfectly seamless (FFT noise is periodic, so tiles wrap).
"""
from __future__ import annotations

import io

import numpy as np
from PIL import Image


def _tileable_noise(n: int, scale: float, rng) -> np.ndarray:
    """Smooth periodic (edge-wrapping) value noise in [0,1]. Larger scale = bigger blobs."""
    white = rng.normal(size=(n, n))
    fx = np.fft.fftfreq(n)[:, None]
    fy = np.fft.fftfreq(n)[None, :]
    r = np.sqrt(fx ** 2 + fy ** 2)
    lowpass = np.exp(-(r * scale) ** 2)          # gaussian low-pass in freq domain
    out = np.fft.ifft2(np.fft.fft2(white) * lowpass).real
    return (out - out.min()) / (np.ptp(out) + 1e-9)


def fog_tile(size: int = 256, seed: int = 7) -> bytes:
    """A seamless, opaque, dark murky fog-of-war tile. Returns PNG bytes."""
    rng = np.random.default_rng(seed)
    # two octaves: broad clouds + finer wisps
    n = _tileable_noise(size, 6.0, rng) * 0.7 + _tileable_noise(size, 14.0, rng) * 0.3
    n = (n - n.min()) / (np.ptp(n) + 1e-9)
    value = 0.20 + n * 0.20                        # dark, low-contrast (0.20–0.40)
    tint = np.array([0.86, 0.90, 1.0])             # faint cool blue-grey
    rgb = np.clip(value[..., None] * tint * 255, 0, 255).astype(np.uint8)
    rgba = np.dstack([rgb, np.full((size, size), 255, np.uint8)])
    buf = io.BytesIO()
    Image.fromarray(rgba, "RGBA").save(buf, "PNG")
    return buf.getvalue()
