// Canvas 2D renderer: flat-shaded tiles, no assets. Reads engine planes each
// frame — the canvas is a pure view of WASM state.
const TERRAIN_COLOR = {
  0: '#7da45c',   // plain
  1: '#3f7fae',   // shallow water
  2: '#274e77',   // deep water
  3: '#8d8577',   // mountain
  4: '#c9b268',   // village
  5: '#c9c2ae',   // city tile
  6: '#4c7a45',   // forest
  7: '#141414',   // fog
};
const PLAYER_COLOR = ['#ff7a76', '#7ab5ff', '#ffd166', '#8ef0a0'];
const RESOURCE_COLOR = { 0: '#bfe3ff', 1: '#ffb3c8', 2: '#e8d9a0', 3: '#9fd8e8', 5: '#6f6a5e', 6: '#f2e28a', 7: '#d8ccff' };

export class Renderer {
  constructor(canvas, game) {
    this.cv = canvas;
    this.ctx = canvas.getContext('2d');
    this.g = game;
  }

  tileSize() { return Math.floor(this.cv.width / this.g.size); }

  draw(ui) {
    const g = this.g, ctx = this.ctx, ts = this.tileSize();
    const terr = g.terrain(), res = g.resource(), bld = g.building(),
          roads = g.roads(), cityAt = g.cityAt();
    ctx.clearRect(0, 0, this.cv.width, this.cv.height);

    for (let t = 0; t < g.tiles; t++) {
      const x = (t % g.size) * ts, y = Math.floor(t / g.size) * ts;
      ctx.fillStyle = TERRAIN_COLOR[terr[t]] || '#000';
      ctx.fillRect(x, y, ts - 1, ts - 1);

      // city borders: tinted edge in the owner's color
      const ci = cityAt[t];
      if (ci >= 0) {
        ctx.strokeStyle = PLAYER_COLOR[g.cityOwner(ci)] + '88';
        ctx.lineWidth = 2;
        ctx.strokeRect(x + 1, y + 1, ts - 3, ts - 3);
      }
      if (roads[t] && terr[t] !== 5) {
        ctx.fillStyle = '#00000055';
        ctx.fillRect(x + ts * 0.4, y, ts * 0.2, ts);
        ctx.fillRect(x, y + ts * 0.4, ts, ts * 0.2);
      }
      if (res[t] >= 0 && res[t] !== 4 && RESOURCE_COLOR[res[t]]) {
        ctx.fillStyle = RESOURCE_COLOR[res[t]];
        ctx.beginPath();
        ctx.arc(x + ts * 0.75, y + ts * 0.25, ts * 0.1, 0, 7);
        ctx.fill();
      }
      if (bld[t] >= 0 && bld[t] !== 19 && terr[t] !== 5) {
        ctx.fillStyle = '#2b2b2b';
        ctx.fillRect(x + ts * 0.15, y + ts * 0.55, ts * 0.3, ts * 0.3);
      }

      // city center: house glyph + level
      if (terr[t] === 5 && ci >= 0) {
        ctx.fillStyle = PLAYER_COLOR[g.cityOwner(ci)];
        ctx.fillRect(x + ts * 0.2, y + ts * 0.2, ts * 0.6, ts * 0.6);
        if (g.cityWalls(ci)) {
          ctx.strokeStyle = '#fff';
          ctx.lineWidth = 2;
          ctx.strokeRect(x + ts * 0.16, y + ts * 0.16, ts * 0.68, ts * 0.68);
        }
        ctx.fillStyle = '#10141a';
        ctx.font = `bold ${ts * 0.35}px monospace`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        const cap = g.cityCapital(ci) ? '★' : '';
        ctx.fillText(`${g.cityLevel(ci)}${cap}`, x + ts / 2, y + ts / 2);
      }

      // unit: circle + hp arc
      const ui_ = g.unitAt(t);
      if (ui_ >= 0) {
        const owner = g.unitOwner(ui_);
        const cx = x + ts / 2, cy = y + ts / 2, r = ts * 0.26;
        ctx.fillStyle = PLAYER_COLOR[owner];
        ctx.beginPath(); ctx.arc(cx, cy, r, 0, 7); ctx.fill();
        ctx.strokeStyle = '#10141a';
        ctx.lineWidth = 1.5;
        ctx.stroke();
        // hp ring
        const hp = g.unitHp(ui_) / g.unitMaxHp(ui_);
        ctx.strokeStyle = hp > 0.5 ? '#c8f7c5' : '#f7c5c5';
        ctx.lineWidth = 2.5;
        ctx.beginPath(); ctx.arc(cx, cy, r + 2.5, -Math.PI / 2, -Math.PI / 2 + hp * 2 * Math.PI);
        ctx.stroke();
        ctx.fillStyle = '#10141a';
        ctx.font = `bold ${ts * 0.3}px monospace`;
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.fillText('WRDSACKMBSBG'[g.unitType(ui_)], cx, cy);
      }
    }

    // selection + legal-target highlights (human mode)
    if (ui && ui.highlightTiles) {
      ctx.fillStyle = '#ffffff2e';
      for (const t of ui.highlightTiles) {
        const x = (t % g.size) * ts, y = Math.floor(t / g.size) * ts;
        ctx.fillRect(x, y, ts - 1, ts - 1);
      }
    }
    const sel = g.selectedTile();
    if (sel >= 0) {
      const x = (sel % g.size) * ts, y = Math.floor(sel / g.size) * ts;
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2.5;
      ctx.strokeRect(x + 1, y + 1, ts - 3, ts - 3);
    }
  }
}
export { PLAYER_COLOR };
