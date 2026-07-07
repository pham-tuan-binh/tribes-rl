// Renderer replicating the Tribes reference pipeline (src/gui/GameView.java):
// the whole board is drawn under a -45° rotation (diamond view). Terrain,
// cities, roads, resources, buildings and the shine are drawn IN the rotated
// frame (the tile art carries the 3D skirts); units and text are drawn
// upright at the rotated anchor points.
export const PLAYER_COLOR = ['#e2413e', '#3f8ee0', '#e0b23f', '#54c96f'];

const DEG45 = Math.PI / 4;
const R = Math.SQRT1_2;   // cos/sin of 45°

export class Renderer {
  constructor(canvas, game, assets, cell = 64) {
    this.cv = canvas;
    this.ctx = canvas.getContext('2d');
    this.g = game;
    this.a = assets;
    this.cell = cell;
    const diag = Math.ceil(game.size * cell * Math.SQRT2);
    canvas.width = diag;
    canvas.height = diag;
    this.H = diag;
    // paper-tone fog tile: unseen board blends into the page instead of
    // reading as a dark backdrop behind the map
    const fog = document.createElement('canvas');
    fog.width = fog.height = cell;
    const fx = fog.getContext('2d');
    fx.fillStyle = '#f4f0e2';   // flat: unseen area reads as one seamless surface
    fx.fillRect(0, 0, cell, cell);
    this.fogTile = fog;
    // static board layer (terrain/city/roads/buildings/territory): rebuilt
    // only when its content signature changes; units + selection draw on top
    this.scv = document.createElement('canvas');
    this.scv.width = this.scv.height = diag;
    this.sctx = this.scv.getContext('2d');
    this.staticSig = null;
    this.frameSig = null;
  }

  // rotated-frame screen coordinates of grid cell (col, row), top-left corner
  rotPoint(col, row) {
    const px = col * this.cell, py = row * this.cell;
    return { x: R * (px + py), y: this.H / 2 + R * (py - px) };
  }
  // inverse: canvas coords -> {col, row}
  unproject(sx, sy) {
    const px = R * sx - R * (sy - this.H / 2);
    const py = R * sx + R * (sy - this.H / 2);
    return { col: Math.floor(px / this.cell), row: Math.floor(py / this.cell) };
  }

  // draw image in the ROTATED frame, centered in cell (col,row), sized px
  drawRot(img, col, row, size, ctx = this.ctx) {
    if (!img) return;
    const c = this.cell;
    ctx.save();
    ctx.translate(0, this.H / 2);
    ctx.rotate(-DEG45);
    ctx.drawImage(img, col * c + (c - size) / 2, row * c + (c - size) / 2, size, size);
    ctx.restore();
  }

  // draw image UPRIGHT (screen-vertical), horizontally centered on tile
  // (col,row) and bottom-anchored near the tile centre. Used for buildings and
  // resources whose sprites are drawn straight (like units), not pre-rotated.
  drawUpright(img, col, row, size, lift = 0.3, ctx = this.ctx) {
    if (!img) return;
    const p = this.rotPoint(col + 0.5, row + 0.5);   // screen centre of the tile
    ctx.drawImage(img, p.x - size / 2, p.y - size * (0.5 + lift), size, size);
  }

  // GameView.getContextImg — edge-variant selection. i = row, j = col.
  contextTerrain(terr, i, j, t) {
    const g = this.g, N = g.size;
    const at = (r, c) => (r < 0 || c < 0 || r >= N || c >= N) ? -1 : terr[r * N + c];
    const isWater = (v) => v === 1 || v === 2;
    const key = (suffix) => this.a.terrainVariant(t, suffix);

    if (t === 1 || t === 2) {   // water tiles
      const down = i === N - 1;
      const top = i > 0 && !isWater(at(i - 1, j));
      const left = j === 0;
      const right = j < N - 1 && !isWater(at(i, j + 1));
      const diagURWater = i > 0 && j < N - 1 && isWater(at(i - 1, j + 1));
      if (down) {
        if (left) return key('down-left');
        if (right) {
          if (top) return key('top-down-right');
          return key(t === 1 && diagURWater ? 'down-right-ur' : 'down-right');
        }
        if (top) return key(t === 1 && diagURWater ? 'top-down-ur' : 'top-down');
        return key('down');
      }
      if (top) {
        if (t === 1 && (j === N - 1 || diagURWater))
          return key(left ? 'top-left-ur' : 'top-ur');
        if (right) return key(left ? 'top-left-right' : 'top-right');
        return key(left ? 'top-left' : 'top');
      }
      if (right) return key(i === 0 || (j < N - 1 && t === 1 && diagURWater) ? 'right-ur' : 'right');
      if (left) return key('left');
      return key(null);
    }

    // land tiles: skirts where the tile borders water/board edge below or left
    const down = i === N - 1 || at(i + 1, j) === 1;
    const left = j === 0 || at(i, j - 1) === 1;
    if (down) {
      if (j === N - 1) return key(i === N - 1 ? 'corner-dr' : 'down-dr');
      if (left) {
        if (j === 0)
          return key(i < N - 1 && isWater(at(i, j + 1)) ? 'down-left-el-dr' : 'down-left-el');
        if (i === N - 1)
          return key(isWater(at(i - 1, j)) ? 'down-left-ed-ul' : 'down-left-ed');
        return key('down-left');
      }
      return key(j < N - 1 && i < N - 1 && isWater(at(i, j + 1)) ? 'down-dr' : 'down');
    }
    if (left) {
      if (i === 0 || isWater(at(i - 1, j))) {
        if (i === 0 && j === 0) return key('corner-ul');
        return key(j === 0 ? 'left' : 'left-ul');
      }
      return key('left');
    }
    const tl = at(i, j - 1), td = at(i + 1, j), tld = at(i + 1, j - 1);
    if (i < N - 1 && j > 0 && isWater(tld) && !isWater(tl) && !isWater(td)) return key('dl');
    return key(null);
  }

  draw(ui) {
    const g = this.g, ctx = this.ctx, c = this.cell, N = g.size;
    const viewer = ui && ui.viewer >= 0 ? ui.viewer : -1;   // -1 = omniscient
    const sel = g.selectedTile();
    let hl = 0;
    if (ui && ui.highlightTiles) for (const t of ui.highlightTiles) hl = (Math.imul(hl, 31) + t + 1) | 0;

    // frame gate: identical state + view since last frame -> draw nothing
    const frameSig = `${g.version}|${viewer}|${sel}|${hl}`;
    if (frameSig === this.frameSig) return;
    this.frameSig = frameSig;

    // per-tile visibility, computed once per changed frame
    const seen = this._seen || (this._seen = new Uint8Array(N * N));
    for (let t = 0; t < N * N; t++) seen[t] = viewer < 0 || g.visible(viewer, t) ? 1 : 0;

    // static layer signature: everything drawn below units (FNV-1a)
    const terr = g.terrain(), res = g.resource(), bld = g.building(),
          roads = g.roads(), cityAt = g.cityAt();
    let h = 0x811c9dc5;
    for (let t = 0; t < N * N; t++) {
      h = Math.imul(h ^ (terr[t] & 0xff), 0x01000193);
      h = Math.imul(h ^ (res[t] & 0xff), 0x01000193);
      h = Math.imul(h ^ (bld[t] & 0xff), 0x01000193);
      h = Math.imul(h ^ roads[t], 0x01000193);
      h = Math.imul(h ^ (cityAt[t] & 0xff), 0x01000193);
      h = Math.imul(h ^ seen[t], 0x01000193);
      const ci = cityAt[t];
      if (ci >= 0 && seen[t]) {
        h = Math.imul(h ^ g.cityOwner(ci), 0x01000193);
        h = Math.imul(h ^ (g.cityLevel(ci) + (g.cityWalls(ci) << 5)), 0x01000193);
      }
    }
    const staticSig = `${h}|${viewer}|${hl}`;
    if (staticSig !== this.staticSig) {
      this.staticSig = staticSig;
      this.renderStatic(ui, seen, terr, res, bld, roads, cityAt);
    }

    ctx.clearRect(0, 0, this.cv.width, this.cv.height);
    ctx.drawImage(this.scv, 0, 0);
    this.renderUnits(seen);

    // selection outline (rotated rect, like highlightTile)
    if (sel >= 0 && seen[sel])
      this.strokeCellRot(sel % N, Math.floor(sel / N), '#ffffff', 2.5);
  }

  // terrain / city / roads / shine / resources / buildings / territory + badges
  renderStatic(ui, seenArr, terr, res, bld, roads, cityAt) {
    const g = this.g, ctx = this.sctx, c = this.cell, a = this.a, N = g.size;
    const seen = (t) => seenArr[t] === 1;
    ctx.clearRect(0, 0, this.scv.width, this.scv.height);

    // 1. terrain (fog for unseen; city tiles use the plain context underneath).
    // Tiles are drawn a hair oversized: under the -45° rotation their edges land
    // on fractional device pixels and antialiasing leaves visible seam lines.
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const t = i * N + j;
      let img;
      if (!seen(t)) img = this.fogTile;
      // village (4) & city (5) are upright sprites over a plain ground tile
      else img = this.contextTerrain(terr, i, j, (terr[t] === 5 || terr[t] === 4) ? 0 : terr[t]);
      this.drawRot(img, j, i, c + 1.5, ctx);
    }

    // 2. village/city sprites on top of their plain base (upright, like buildings)
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const t = i * N + j;
      if (seen(t) && (terr[t] === 5 || terr[t] === 4)) {
        this.drawUpright(a.terrain[terr[t]], j, i, c * 1.1, 0.34, ctx);
        const ci = cityAt[t];
        if (ci >= 0 && g.cityWalls(ci)) this.drawRot(a.misc.walls, j, i, c, ctx);
      }
    }

    // 3. roads: half-segment toward each connected neighbour (GameView.paintRoads)
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const t = i * N + j;
      if (!roads[t] || !seen(t)) continue;
      let any = false;
      for (let di = -1; di <= 1; di++) for (let dj = -1; dj <= 1; dj++) {
        if (!di && !dj) continue;
        const ni = i + di, nj = j + dj;
        if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
        if (!roads[ni * N + nj]) continue;
        any = true;
        const diagonal = di !== 0 && dj !== 0;
        const angle = Math.atan2(di, dj) + DEG45;   // atan2(dx,dy) + (iso+90)°
        const p = this.rotPoint(j, i);
        const x = p.x + c / 4, y = p.y - c / 2;
        const ax = x + c / 2, ay = y + c / 2;
        const img = diagonal ? a.misc.roadD : a.misc.roadV;
        ctx.save();
        ctx.translate(ax, ay);
        ctx.rotate(diagonal ? angle - DEG45 : angle);
        ctx.drawImage(img, x - ax, y - ay, c, c);
        ctx.restore();
      }
      if (!any && terr[t] !== 5 && bld[t] !== 0)   // lone road dot (not city/port)
        this.drawRot(a.misc.roadV, j, i, c, ctx);
    }

    // 4. shine + resources + buildings (rotated frame, GameView sizes)
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const t = i * N + j;
      if (!seen(t)) continue;
      if (ui && ui.highlightTiles && ui.highlightTiles.has(t))
        this.drawRot(a.misc.shine, j, i, c, ctx);
      if (res[t] >= 0 && res[t] !== 4 && a.resource[res[t]])
        this.drawUpright(a.resource[res[t]], j, i, c * 0.8, 0.12, ctx);
      const b = bld[t];
      if (b >= 0 && b !== 19) {
        const big = b !== 5 && !(b >= 8 && b <= 11);   // customs house & temples smaller
        this.drawUpright(a.building[b], j, i, big ? c * 1.1 : c * 0.85, 0.34, ctx);
      }
    }

    // 5. city territory border + level badge (upright)
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const t = i * N + j;
      const ci = cityAt[t];
      if (ci < 0 || !seen(t)) continue;
      this.strokeCellRot(j, i, PLAYER_COLOR[g.cityOwner(ci)] + '55', 1.5, ctx);
      if (terr[t] === 5) {
        const p = this.rotPoint(j, i);
        const label = `${g.cityLevel(ci)}${g.cityCapital(ci) ? '★' : ''}`;
        const bw = c * 0.4, bh = c * 0.22;
        const bx = p.x + c * R - bw / 2, by = p.y + c * 0.32;
        ctx.fillStyle = PLAYER_COLOR[g.cityOwner(ci)];
        ctx.fillRect(bx, by, bw, bh);
        ctx.fillStyle = '#fff';
        ctx.font = `bold ${bh * 0.78}px sans-serif`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(label, bx + bw / 2, by + bh / 2 + 1);
      }
    }
  }

  // units: the per-frame dynamic layer (upright sprites at rotated anchors)
  renderUnits(seenArr) {
    const g = this.g, ctx = this.ctx, c = this.cell, a = this.a, N = g.size;
    for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
      const t = i * N + j;
      if (!seenArr[t]) continue;
      const u = g.unitAt(t);
      if (u < 0) continue;
      const type = g.unitType(u);
      const imgSize = (type === 11 || type === 5) ? c : c * 0.75;   // giant/catapult bigger
      const p = this.rotPoint(j, i);
      const x = p.x + (c * c) / 4 / imgSize;
      const y = p.y - imgSize / 1.5;
      const owner = g.unitOwner(u);
      const spent = g.unitStatus(u) === 5;
      // sprites are keyed by SEAT color (new art pack: 4 team colors), so a
      // unit's color always matches its player's UI color
      const img = a.unitSprite(type, owner, spent);
      if (img) ctx.drawImage(img, x, y, imgSize, imgSize);
      if (type >= 8 && type <= 10) {   // naval: mini carried land unit
        const carried = a.unitSprite(0, owner, spent);
        if (carried) ctx.drawImage(carried, x + c / 4, y + c / 4, imgSize / 2, imgSize / 2);
      }
      // HP + team in ONE badge: seat-colored box with the current HP,
      // white-rimmed so it reads on any terrain (style of the city badges)
      const hp = g.unitHp(u), maxHp = g.unitMaxHp(u);
      const bh = c * 0.24, bw = Math.max(c * 0.3, bh * 0.6 + String(hp).length * bh * 0.42);
      const bx = x + imgSize - bw * 0.55, by = y - bh * 0.35;
      ctx.fillStyle = '#fffbf0';
      ctx.fillRect(bx - 1.5, by - 1.5, bw + 3, bh + 3);
      ctx.fillStyle = PLAYER_COLOR[owner];
      ctx.fillRect(bx, by, bw, bh);
      ctx.fillStyle = '#fff';
      ctx.font = `bold ${Math.round(bh * 0.75)}px sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(`${hp}`, bx + bw / 2, by + bh / 2 + 1);
      // damage: thin health bar under the badge, drains left to right
      if (hp < maxHp) {
        ctx.fillStyle = 'rgba(23,23,23,.55)';
        ctx.fillRect(bx, by + bh + 1.5, bw, 3);
        ctx.fillStyle = '#fff';
        ctx.fillRect(bx, by + bh + 1.5, bw * (hp / maxHp), 3);
      }
    }
  }

  strokeCellRot(col, row, style, width, ctx = this.ctx) {
    const c = this.cell;
    ctx.save();
    ctx.translate(0, this.H / 2);
    ctx.rotate(-DEG45);
    ctx.strokeStyle = style;
    ctx.lineWidth = width;
    ctx.strokeRect(col * c + 0.5, row * c + 0.5, c - 1, c - 1);
    ctx.restore();
  }
}
