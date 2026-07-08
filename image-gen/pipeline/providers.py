"""Image-generation backends — two providers, one call.

    render(prompt, ref_path, *, provider, model, size, transparent, fidelity) -> PNG bytes

openai      gpt-image-1 via the OpenAI SDK (images.generate / images.edit).
            Native transparent background + input_fidelity control. Key:
            OPENAI_API_KEY.

openrouter  the OpenRouter Unified Image API (POST /api/v1/images) — one key for
            many models. Default is `openai/gpt-image-2` (OpenAI's image model
            routed through OpenRouter). Reference images ride in
            `input_references` as base64 data URIs. Key: OPENROUTER_API_KEY.

Note on transparency: we don't depend on it here. Sprites are rendered on a
plain background and made transparent downstream by the rembg pass
(pipeline/bg_remove.py), so every provider/model is treated the same.
"""
from __future__ import annotations

import base64
import os
import time
from pathlib import Path

from . import openai_client

OPENROUTER_URL = "https://openrouter.ai/api/v1/images"

DEFAULT_MODEL = {
    "openai": "gpt-image-1",
    # Gemini 3.1 Flash Image ("Nano Banana 2") — best AoE-style realism + reference
    # editing. It's multi-provider and OpenRouter's images endpoint does NOT reliably
    # honor provider routing, so calls randomly land on geo-blocked "Google AI Studio".
    # We retry (see _RETRY_ERRORS) so the coin flip resolves to a working route.
    # Geo-safe alternatives (no retry needed): bytedance-seed/seedream-4.5 (voxel-ish),
    # black-forest-labs/flux.2-pro. openai/gpt-image-2 needs a US VPN.
    "openrouter": "google/gemini-3.1-flash-image",
}

# Substrings marking a transient/routing error worth retrying (vs. a real 400).
_RETRY_ERRORS = ("not supported", "429", "rate limit", "502", "503", "504",
                 "timeout", "temporarily")

# map our WxH size to OpenRouter's resolution tier + aspect ratio
_OR_ASPECT = {(1024, 1024): "1:1", (1024, 1536): "2:3", (1536, 1024): "3:2"}


def _data_uri(path) -> str:
    return "data:image/png;base64," + base64.b64encode(Path(path).read_bytes()).decode()


def _openrouter(prompt: str, refs, *, model: str, size: str,
                quality: str, ignore=None) -> bytes:
    import httpx
    key = os.environ.get("OPENROUTER_API_KEY")
    if not key:
        raise RuntimeError(
            "OPENROUTER_API_KEY is not set. Add it to image-gen/.env or export it."
        )
    w, h = (int(x) for x in size.split("x"))
    body = {
        "model": model,
        "prompt": prompt,
        "n": 1,
        "resolution": "2K",  # Seedream needs >=~3.69M px; 2K works for all models
        "aspect_ratio": _OR_ASPECT.get((w, h), "1:1"),
        "output_format": "png",
        "quality": quality,
    }
    if ignore:
        # OpenRouter fans a model out across upstream providers; some geo-block
        # certain regions (e.g. google-ai-studio -> "location not supported").
        # Excluding those makes routing deterministic; fallbacks cover the rest.
        body["provider"] = {"ignore": list(ignore), "allow_fallbacks": True}
    if refs:
        body["input_references"] = [
            {"type": "image_url", "image_url": {"url": _data_uri(p)}} for p in refs
        ]
    # generous read timeout: image models with several input references can
    # take minutes. Transport errors are wrapped as RuntimeError("... timeout")
    # so the retry loop in render() treats them as transient.
    try:
        resp = httpx.post(OPENROUTER_URL, json=body,
                          timeout=httpx.Timeout(10.0, read=600.0, write=120.0, pool=10.0),
                          headers={"Authorization": f"Bearer {key}"})
    except (httpx.TimeoutException, httpx.TransportError) as e:
        raise RuntimeError(f"OpenRouter transport timeout/error for {model!r}: {e}") from e
    if resp.status_code >= 400:
        # surface OpenRouter's JSON error message, not just the status code
        try:
            msg = resp.json().get("error", resp.text)
        except Exception:
            msg = resp.text
        raise RuntimeError(f"OpenRouter {resp.status_code} for model {model!r}: {msg}")
    return base64.b64decode(resp.json()["data"][0]["b64_json"])


# Upstream providers to exclude by default — these geo-block some regions.
DEFAULT_OR_IGNORE = ["google-ai-studio"]


def render(prompt: str, refs=None, *,
           provider: str = "openai", model: str | None = None,
           size: str = "1024x1024", transparent: bool = False,
           quality: str = "high", fidelity: str = "low",
           or_ignore=None, retries: int = 10) -> bytes:
    """Generate one image. Returns PNG bytes.

    refs  ordered list of reference image paths (original asset, terrain
          context, art-style screenshot, ...). The prompt should explain what
          each attached reference means, in the same order.
    """
    model = model or DEFAULT_MODEL[provider]
    refs = [p for p in (refs or []) if p]
    if provider == "openai":
        if refs:
            return openai_client.edit(prompt, refs, model=model, size=size,
                                      transparent=transparent, quality=quality,
                                      input_fidelity=fidelity)
        return openai_client.generate(prompt, model=model, size=size,
                                      transparent=transparent, quality=quality)
    if provider == "openrouter":
        ignore = DEFAULT_OR_IGNORE if or_ignore is None else or_ignore
        last = None
        for attempt in range(retries):
            try:
                return _openrouter(prompt, refs, model=model, size=size,
                                   quality=quality, ignore=ignore)
            except RuntimeError as e:
                last = e
                if not any(s in str(e).lower() for s in _RETRY_ERRORS):
                    raise  # a real error (bad params, auth) — don't hammer it
                time.sleep(min(2.0, 0.4 * (attempt + 1)))
        raise last
    raise ValueError(f"unknown provider {provider!r}")
