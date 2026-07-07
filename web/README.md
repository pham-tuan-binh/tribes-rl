# polytopia-rl website

One page, two modes:

- **Agents** — watch agents play each other, speed slider from slow-mo to unthrottled
- **Human** — you are red; click a highlighted tile to select, pick an action, click a target

No framework, no bundler, no npm: plain ES-module JavaScript over a WASM build
of the same C engine the agents train on (`web/wasm/bridge.c` wraps
`puffer/polytopia_env.h`, so the browser game and the training env are the
same simulation, bit for bit).

## Build & run

```sh
brew install emscripten   # once (or any emcc >= 3.x)
./build.sh                # -> dist/poly.js + dist/poly.wasm (~56 KB)
python3 -m http.server -d . 8080
# open http://localhost:8080
```

## Layout

- `wasm/bridge.c` — the exported C ABI (new game / mask / act / state accessors)
- `js/game.js` — WASM wrapper + name tables (mirrors the env's verb layout)
- `js/render.js` — Canvas 2D renderer, flat-shaded, no assets
- `js/agent.js` — agent policies (random placeholder; trained weights TODO M3)
- `js/main.js` — modes, loop, input
