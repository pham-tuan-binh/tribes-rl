"""Thin wrapper over the OpenAI Images API (gpt-image-1).

Two paths:
  * edit()     — reference-guided: pass the existing game asset as an input
                 image so silhouette / scale / framing carry over. This is the
                 default and the reason outputs "look like" the originals.
  * generate() — pure text-to-image, when there is no reference.

gpt-image-1 can emit a transparent background directly (`background`), so most
sprites come out already cut-out; bg_remove.py is a cleanup safety net.

The only thing you configure is OPENAI_API_KEY (see .env.example).
"""
from __future__ import annotations

import base64
import os
from pathlib import Path

MODEL = "gpt-image-1"


def _client():
    # Imported lazily so --dry-run works without the SDK installed.
    from openai import OpenAI
    key = os.environ.get("OPENAI_API_KEY")
    if not key:
        raise RuntimeError(
            "OPENAI_API_KEY is not set. Copy image-gen/.env.example to .env and "
            "fill it in, or `export OPENAI_API_KEY=...`."
        )
    return OpenAI(api_key=key)


def _decode(resp) -> bytes:
    return base64.b64decode(resp.data[0].b64_json)


def generate(prompt: str, *, model: str = MODEL, size: str = "1024x1024",
             transparent: bool = True, quality: str = "high") -> bytes:
    """Text-to-image. Returns raw PNG bytes."""
    kwargs = dict(model=model, prompt=prompt, size=size, n=1, quality=quality)
    if transparent:
        kwargs["background"] = "transparent"
    return _decode(_client().images.generate(**kwargs))


def edit(prompt: str, ref_paths, *, model: str = MODEL,
         size: str = "1024x1024", transparent: bool = True, quality: str = "high",
         input_fidelity: str = "low") -> bytes:
    """Reference-guided image. `ref_paths` is one path or a list (e.g. the
    existing asset + a style reference). Returns PNG bytes.

    input_fidelity="low" keeps the references as loose guidance (we want *new*
    art, not a faithful copy). Bump to "high" to hug the originals tightly.
    """
    if isinstance(ref_paths, (str, Path)):
        ref_paths = [ref_paths]
    handles = [Path(p).open("rb") for p in ref_paths]
    try:
        kwargs = dict(model=model, prompt=prompt, image=handles, size=size, n=1,
                      quality=quality, input_fidelity=input_fidelity)
        if transparent:
            kwargs["background"] = "transparent"
        return _decode(_client().images.edit(**kwargs))
    finally:
        for h in handles:
            h.close()
