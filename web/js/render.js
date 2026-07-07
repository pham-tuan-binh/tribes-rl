// Sprite renderer mirroring the Tribes GUI layer order (GameView.java):
// terrain -> roads -> resources -> buildings -> city overlays -> units.
export const PLAYER_COLOR = ['#e2413e', '#3f8ee0', '#e0b23f', '#54c96f'];

export class Renderer {
  constructor(canvas, game, assets) {
    this.cv = canvas;
    this.ctx = canvas.getContext('2d');
    this.ctx.imageSmoothingEnabled = false;
    this.g = game;
    this.a = assets;
  }

  tileSize() { return Math.floor(this.cv.width / this.g.size); }

  drawTile(img, t, ts) {
    if (!img) return;
    const x = (t % this.g.size) * ts, y = Math.floor(t / this.g.size) * ts;
    this.ctx.drawImage(img, x, y, ts, ts);
  }
  drawTileScaled(img, t, ts, scale) {
    if (!img) return;
    const x = (t % this.g.size) * ts, y = Math.floor(t / this.g.size) * ts;
    const s = ts * scale, off = (ts - s) / 2;
    this.ctx.drawImage(img, x + off, y + off, s, s);
  }

  draw(ui) {
    const g = this.g, ctx = this.ctx, ts = this.tileSize(), a = this.a;
    const terr = g.terrain(), res = g.resource(), bld = g.building(),
          roads = g.roads(), cityAt = g.cityAt();
    ctx.clearRect(0, 0, this.cv.width, this.cv.height);

    // 1. terrain
    for (let t = 0; t < g.tiles; t++) this.drawTile(a.terrain[terr[t]], t, ts);

    // 2. roads (not on city centers, matching the GUI)
    for (let t = 0; t < g.tiles; t++)
      if (roads[t] && terr[t] !== 5) this.drawTileScaled(a.misc.road, t, ts, 0.9);

    // 3. resources
    for (let t = 0; t < g.tiles; t++)
      if (res[t] >= 0 && res[t] !== 4 && a.resource[res[t]])
        this.drawTileScaled(a.resource[res[t]], t, ts, 0.62);

    // 4. buildings
    for (let t = 0; t < g.tiles; t++)
      if (bld[t] >= 0 && bld[t] !== 19)
        this.drawTileScaled(a.building[bld[t]], t, ts, 0.78);

    // 5. city territory tint + city overlays (owner ring, walls, level badge)
    for (let t = 0; t < g.tiles; t++) {
      const ci = cityAt[t];
      if (ci < 0) continue;
      const x = (t % g.size) * ts, y = Math.floor(t / g.size) * ts;
      const color = PLAYER_COLOR[g.cityOwner(ci)];
      ctx.strokeStyle = color + '66';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(x + 0.5, y + 0.5, ts - 1, ts - 1);
      if (terr[t] === 5) {
        if (g.cityWalls(ci)) this.drawTile(a.misc.walls, t, ts);
        // level badge
        const label = `${g.cityLevel(ci)}${g.cityCapital(ci) ? '★' : ''}`;
        ctx.fillStyle = color;
        const bw = ts * 0.42, bh = ts * 0.24;
        ctx.fillRect(x + (ts - bw) / 2, y + ts - bh - 1, bw, bh);
        ctx.fillStyle = '#fff';
        ctx.font = `bold ${bh * 0.8}px sans-serif`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(label, x + ts / 2, y + ts - bh / 2 - 1);
      }
    }

    // 6. shine on actionable tiles (human mode)
    if (ui && ui.highlightTiles)
      for (const t of ui.highlightTiles) this.drawTileScaled(a.misc.shine, t, ts, 1.0);

    // 7. units — per-tribe sprite; Exhausted variant when the unit is spent
    for (let t = 0; t < g.tiles; t++) {
      const u = g.unitAt(t);
      if (u < 0) continue;
      const owner = g.unitOwner(u);
      const spent = g.unitStatus(u) === 5;   // FINISHED
      const img = a.unitSprite(g.unitType(u), g.tribe(owner), spent);
      this.drawTileScaled(img, t, ts, 0.85);
      // hp bar
      const x = (t % g.size) * ts, y = Math.floor(t / g.size) * ts;
      const hp = g.unitHp(u) / g.unitMaxHp(u);
      if (hp < 1) {
        ctx.fillStyle = '#00000088';
        ctx.fillRect(x + ts * 0.15, y + 2, ts * 0.7, 4);
        ctx.fillStyle = hp > 0.5 ? '#6fe06f' : '#e06f6f';
        ctx.fillRect(x + ts * 0.15, y + 2, ts * 0.7 * hp, 4);
      }
      // owner dot
      ctx.fillStyle = PLAYER_COLOR[owner];
      ctx.beginPath();
      ctx.arc(x + ts * 0.85, y + ts * 0.18, ts * 0.07, 0, 7);
      ctx.fill();
      if (g.unitVeteran(u)) {
        ctx.fillStyle = '#ffd700';
        ctx.font = `${ts * 0.25}px sans-serif`;
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';
        ctx.fillText('★', x + 2, y + 1);
      }
    }

    // 8. selection outline
    const sel = g.selectedTile();
    if (sel >= 0) {
      const x = (sel % g.size) * ts, y = Math.floor(sel / g.size) * ts;
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2.5;
      ctx.strokeRect(x + 1, y + 1, ts - 3, ts - 3);
    }
  }
}
