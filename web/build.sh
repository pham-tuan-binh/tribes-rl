#!/usr/bin/env bash
# Build the WASM engine module for the website.
# Output: web/dist/poly.js + poly.wasm (ES module; import createPoly from it).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist
emcc -O3 -std=c11 \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPoly \
    -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
    -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,HEAPU8 \
    -o dist/poly.js \
    wasm/bridge.c
echo "built web/dist/poly.js ($(du -h dist/poly.wasm | cut -f1) wasm)"
