#!/usr/bin/env bash
# Build the WASM engine for the website. One module serves every player
# count: the env is player-agnostic (pooled-opponent obs, runtime count).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist
rm -f dist/poly2.* dist/poly3.* dist/poly4.*   # pre-agnostic per-count builds
emcc -O3 -std=c11 \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPoly \
    -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
    -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,HEAPU8 \
    -o dist/poly.js \
    wasm/bridge.c
# manifest of existing sprites: the loader only requests listed files instead
# of probing every optional variant (hundreds of 404s otherwise)
(cd assets && find . -name '*.png' | sed 's|^\./||' | sort \
  | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read().split()))' > manifest.json)
# sprite atlas (one download instead of ~240); atlas.png/json are committed,
# so this step just refreshes them when Pillow is available
python3 pack_atlas.py 2>/dev/null || echo "(pack_atlas skipped: Pillow not available)"
echo "built dist/poly.js ($(du -h dist/poly.wasm | cut -f1)) + assets/manifest.json"
