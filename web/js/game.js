// Thin wrapper over the WASM engine (web/wasm/bridge.c exports).
// All game logic lives in C; this file only marshals memory views.
// One module per player count (compile-time layouts): dist/poly{2,3,4}.js.

export const TERRAIN = ['plain', 'shallow', 'deep', 'mountain', 'village', 'city', 'forest', 'fog'];
export const RESOURCE = ['fish', 'fruit', 'animal', 'whales', '', 'ore', 'crops', 'ruins'];
export const UNITS = ['warrior', 'rider', 'defender', 'swordman', 'archer', 'catapult',
  'knight', 'mindbender', 'boat', 'ship', 'battleship', 'giant'];
export const TECHS = ['climbing', 'fishing', 'hunting', 'organization', 'riding', 'archery',
  'farming', 'forestry', 'free spirit', 'meditation', 'mining', 'roads', 'sailing', 'shields',
  'whaling', 'aquatism', 'chivalry', 'construction', 'mathematics', 'navigation', 'smithery',
  'spiritualism', 'trade', 'philosophy'];
export const BUILDINGS = ['port', 'mine', 'forge', 'farm', 'windmill', 'customs house',
  'lumber hut', 'sawmill', 'temple', 'water temple', 'forest temple', 'mountain temple',
  'altar of peace', "emperor's tomb", 'eye of god', 'gate of power', 'grand bazar',
  'park of fortune', 'tower of wisdom'];
export const LEVELUPS = ['workshop', 'explorer', 'city wall', '+5 stars', '+3 population',
  'border growth', 'park', 'giant'];

// Verb slot layout — must mirror puffer/polytopia_env.h
export const V = {
  END_TURN: 0, RESEARCH0: 1, BUILD_ROAD: 25, UNIT0: 26, BUILD0: 36,
  SPAWN0: 55, LEVELUP0: 63, GATHER: 71, CLEAR_FOREST: 72, BURN_FOREST: 73,
  GROW_FOREST: 74, DESTROY: 75, N: 76,
};
export const UNIT_VERBS = ['move', 'attack', 'capture', 'recover', 'heal others',
  'convert', 'make veteran', 'upgrade', 'disband', 'examine ruins'];

export function verbName(v) {
  if (v === V.END_TURN) return 'end turn';
  if (v >= V.RESEARCH0 && v < V.RESEARCH0 + 24) return `research ${TECHS[v - V.RESEARCH0]}`;
  if (v === V.BUILD_ROAD) return 'build road';
  if (v >= V.UNIT0 && v < V.UNIT0 + 10) return UNIT_VERBS[v - V.UNIT0];
  if (v >= V.BUILD0 && v < V.BUILD0 + 19) return `build ${BUILDINGS[v - V.BUILD0]}`;
  if (v >= V.SPAWN0 && v < V.SPAWN0 + 8) return `spawn ${UNITS[v - V.SPAWN0]}`;
  if (v >= V.LEVELUP0 && v < V.LEVELUP0 + 8) return `level up: ${LEVELUPS[v - V.LEVELUP0]}`;
  if (v === V.GATHER) return 'gather resource';
  if (v === V.CLEAR_FOREST) return 'clear forest';
  if (v === V.BURN_FOREST) return 'burn forest';
  if (v === V.GROW_FOREST) return 'grow forest';
  if (v === V.DESTROY) return 'destroy building';
  return `verb ${v}`;
}

export class Game {
  static async load() {
    const g = new Game();
    // one module for every player count (player-agnostic engine)
    const { default: createPoly } = await import('../dist/poly.js');
    g.m = await createPoly();
    const f = (name, ret, args) => g.m.cwrap(name, ret, args);
    g.newGameRaw = f('poly_new_game', null, ['number', 'number', 'number']);
    g.visible = f('poly_visible', 'number', ['number', 'number']);
    // every state mutation flows through act/cancel/newGame; the version
    // counter lets the renderer skip frames where nothing changed
    g.version = 0;
    const rawCancel = f('poly_cancel', null, []);
    g.cancel = () => { g.version++; rawCancel(); };
    const rawAct = f('poly_act', 'number', ['number']);
    g.act = (a) => { g.version++; return rawAct(a); };
    g.agentAct = f('poly_agent_act', 'number', ['number']);
    g.hasAgent = f('poly_has_agent', 'number', []);
    g.weightsAlloc = f('poly_weights_alloc', 'number', ['number']);
    g.weightsLoad = f('poly_weights_load', 'number', ['number']);
    g.maskPtr = f('poly_mask', 'number', ['number']);
    g.terrainPtr = f('poly_terrain', 'number', []);
    g.resourcePtr = f('poly_resource', 'number', []);
    g.buildingPtr = f('poly_building', 'number', []);
    g.roadsPtr = f('poly_roads', 'number', []);
    g.cityAtPtr = f('poly_city_at', 'number', []);
    g.activePlayer = f('poly_active_player', 'number', []);
    g.phase = f('poly_phase', 'number', []);
    g.selectedTile = f('poly_selected_tile', 'number', []);
    g.tick = f('poly_tick', 'number', []);
    g.gameOver = f('poly_game_over', 'number', []);
    g.result = f('poly_result', 'number', ['number']);
    g.eliminated = f('poly_eliminated', 'number', ['number']);
    g.stars = f('poly_stars', 'number', ['number']);
    g.score = f('poly_score', 'number', ['number']);
    g.income = f('poly_income', 'number', ['number']);
    g.tribe = f('poly_tribe', 'number', ['number']);
    g.techs = f('poly_techs', 'number', ['number']);
    g.techCost = f('poly_tech_cost', 'number', ['number', 'number']);
    g.unitAt = f('poly_unit_at', 'number', ['number']);
    g.unitType = f('poly_unit_type', 'number', ['number']);
    g.unitOwner = f('poly_unit_owner', 'number', ['number']);
    g.unitHp = f('poly_unit_hp', 'number', ['number']);
    g.unitMaxHp = f('poly_unit_max_hp', 'number', ['number']);
    g.unitStatus = f('poly_unit_status', 'number', ['number']);
    g.unitVeteran = f('poly_unit_veteran', 'number', ['number']);
    g.cityOwner = f('poly_city_owner', 'number', ['number']);
    g.cityLevel = f('poly_city_level', 'number', ['number']);
    g.cityPop = f('poly_city_pop', 'number', ['number']);
    g.cityPopNeed = f('poly_city_pop_need', 'number', ['number']);
    g.cityWalls = f('poly_city_walls', 'number', ['number']);
    g.cityCapital = f('poly_city_capital', 'number', ['number']);
    g.size = f('poly_map_size', 'number', [])();
    g.tiles = g.size * g.size;
    g.actionN = f('poly_action_n', 'number', [])();
    g.obsSize = f('poly_obs_size', 'number', [])();
    g.numPlayers = f('poly_num_players', 'number', []);
    g.players = 2;   // refreshed after newGame
    return g;
  }

  newGame(seed, players = this.players || 2, fog = true) {
    this.version++;
    this.newGameRaw(seed >>> 0, players, fog ? 1 : 0);
    this.players = this.numPlayers();
  }

  // fresh views each call: WASM memory may grow and detach old buffers
  bytes(ptr, len) { return new Uint8Array(this.m.HEAPU8.buffer, ptr, len); }
  i8(ptr, len) { return new Int8Array(this.m.HEAPU8.buffer, ptr, len); }

  mask(player) { return this.bytes(this.maskPtr(player), this.actionN); }
  terrain() { return this.i8(this.terrainPtr(), this.tiles); }
  resource() { return this.i8(this.resourcePtr(), this.tiles); }
  building() { return this.i8(this.buildingPtr(), this.tiles); }
  roads() { return this.bytes(this.roadsPtr(), this.tiles); }
  cityAt() { return this.i8(this.cityAtPtr(), this.tiles); }

  legalActions(player) {
    const m = this.mask(player), out = [];
    for (let i = 0; i < this.actionN; i++) if (m[i]) out.push(i);
    return out;
  }

  // fetch trained weights (native trainer .bin); returns true on success.
  // weights are per player count (obs dims differ): latest_3p.bin etc.,
  // with latest.bin as the 2-player default.
  async loadWeights(onProgress = null) {
    const url = 'weights/latest.bin';   // one policy for every player count
    try {
      const resp = await fetch(url);
      if (!resp.ok) return false;
      let data;
      const total = +resp.headers.get('content-length') || 0;
      if (resp.body && total && onProgress) {
        // stream so the loading screen can show real byte progress on the
        // one download that dominates startup
        const reader = resp.body.getReader();
        const chunks = [];
        let got = 0;
        for (;;) {
          const { done, value } = await reader.read();
          if (done) break;
          chunks.push(value);
          got += value.length;
          onProgress(Math.min(1, got / total), got, total);
        }
        data = new Uint8Array(got);
        let off = 0;
        for (const c of chunks) { data.set(c, off); off += c.length; }
      } else {
        data = new Uint8Array(await resp.arrayBuffer());
      }
      data = this.dequantize(data);
      const ptr = this.weightsAlloc(data.length);
      this.m.HEAPU8.set(data, ptr);
      return this.weightsLoad(data.length) === 1;
    } catch {
      return false;
    }
  }

  // fp16-quantized checkpoints (tools/quantize_fp16.py) are half the download;
  // expand back to the fp32 the engine expects. Detected by byte size against
  // the known architectures.
  dequantize(data) {
    const obs = this.obsSize, A = this.actionN;
    const floats = (h, l) => h * obs + (A + 1) * h + 3 * l * h * h;
    const CAND = [[512, 3], [768, 4], [1024, 3]];
    if (CAND.some(([h, l]) => floats(h, l) * 4 === data.length)) return data;  // fp32
    if (!CAND.some(([h, l]) => floats(h, l) * 2 === data.length)) return data; // unknown: engine will refuse
    const u16 = new Uint16Array(data.buffer, data.byteOffset, data.length / 2);
    const out = new Float32Array(u16.length);
    const bits = new Uint32Array(1);
    const f32 = new Float32Array(bits.buffer);
    for (let i = 0; i < u16.length; i++) {
      const h = u16[i];
      const s = (h & 0x8000) << 16;
      let e = (h >> 10) & 0x1f;
      let m = h & 0x3ff;
      if (e === 0) {
        if (m === 0) { bits[0] = s; }
        else {                       // subnormal half -> normal float
          e = 113;
          do { m <<= 1; e--; } while ((m & 0x400) === 0);
          bits[0] = s | (e << 23) | ((m & 0x3ff) << 13);
        }
      } else if (e === 31) { bits[0] = s | 0x7f800000 | (m << 13); }
      else { bits[0] = s | ((e + 112) << 23) | (m << 13); }
      out[i] = f32[0];
    }
    return new Uint8Array(out.buffer);
  }
}
