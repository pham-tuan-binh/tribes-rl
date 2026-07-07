#!/usr/bin/env bash
# Build the WASM engine modules for the website — one per player count
# (obs/agent layouts are compile-time). Output: dist/poly2.js, poly3.js, poly4.js.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist
for N in 2 3 4; do
  emcc -O3 -std=c11 -DENV_PLAYERS=$N \
      -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPoly \
      -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
      -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,HEAPU8 \
      -o "dist/poly$N.js" \
      wasm/bridge.c
done
# manifest of existing sprites: the loader only requests listed files instead
# of probing every optional variant (hundreds of 404s otherwise)
(cd assets && find . -name '*.png' | sed 's|^\./||' | sort \
  | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read().split()))' > manifest.json)
echo "built dist/poly{2,3,4}.js ($(du -h dist/poly2.wasm | cut -f1) each) + assets/manifest.json"
