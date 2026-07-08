"""Background removal — the step that makes sprites transparent.

Image models render the subject on an opaque background (see style.TECH_SPRITE,
which asks for a plain flat neutral one). `rembg` (U^2-Net, runs locally, free,
no per-image cost) is our tool of choice to segment the subject and drop the
background. This is a mandatory pass for cut-out sprite categories.

Strategy: `always` (default) runs rembg on every sprite. `auto` skips it only
when the border already looks transparent (e.g. a model that did emit alpha).
`never` leaves the raw image untouched.
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


import threading

_SESSION = None
_SESSION_LOCK = threading.Lock()


def _rembg(png_bytes: bytes) -> bytes | None:
    """Run rembg if importable; else None (graceful skip).

    One shared U^2-Net session (created under a lock): loading the model per
    call is slow and memory-heavy, and generation now runs multi-threaded.
    onnxruntime sessions are thread-safe for inference.
    """
    global _SESSION
    try:
        from rembg import new_session, remove  # lazy import: keeps --dry-run fast
    except ImportError:
        print("      (rembg unavailable — leaving background intact; run "
              "`uv sync` to install it)")
        return None
    with _SESSION_LOCK:
        if _SESSION is None:
            _SESSION = new_session()
    return remove(png_bytes, session=_SESSION)


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
