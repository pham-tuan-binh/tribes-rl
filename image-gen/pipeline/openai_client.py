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


def generate(prompt: str, *, size: str = "1024x1024",
             transparent: bool = True, quality: str = "high") -> bytes:
    """Text-to-image. Returns raw PNG bytes."""
    kwargs = dict(model=MODEL, prompt=prompt, size=size, n=1, quality=quality)
    if transparent:
        kwargs["background"] = "transparent"
    return _decode(_client().images.generate(**kwargs))


def edit(prompt: str, ref_path: str | Path, *, size: str = "1024x1024",
         transparent: bool = True, quality: str = "high",
         input_fidelity: str = "low") -> bytes:
    """Reference-guided image. `ref_path` is the existing asset. Returns PNG bytes.

    input_fidelity="low" keeps the reference as loose compositional guidance
    (we want *new* realistic art, not a faithful copy of the old sprite). Bump
    to "high" if you want the new art to hug the original silhouette tightly.
    """
    ref = Path(ref_path)
    with ref.open("rb") as fh:
        kwargs = dict(model=MODEL, prompt=prompt, image=[fh], size=size, n=1,
                      quality=quality, input_fidelity=input_fidelity)
        if transparent:
            kwargs["background"] = "transparent"
        return _decode(_client().images.edit(**kwargs))
