// Sprite loading — asset paths mirror the Tribes reference GUI
// (reference/Tribes/src/core/Types.java imageFile mappings). Terrain tiles
// come in edge variants (e.g. plain-down-left.png) selected by neighborhood;
// missing variants fall back to the base tile.
const TERRAIN_BASE = ['plain', 'water', 'deepwater', 'mountain3', 'village2', 'city3', 'forest2'];
const TERRAIN_VARIANTS = [
  'down', 'left', 'top', 'right', 'down-left', 'down-right', 'down-dr', 'top-down',
  'top-left', 'top-right', 'top-ur', 'right-ur', 'left-ul', 'corner-dr', 'corner-ul',
  'down-left-el', 'down-left-ed', 'down-left-el-dr', 'down-left-ed-ul', 'dl',
  'top-down-right', 'top-down-ur', 'top-left-right', 'top-left-ur', 'down-right-ur',
];
const RESOURCE_FILES = ['fish2', 'fruit2', 'animal2', 'whale2', null, 'ore2', 'crops2', 'ruins2'];
const BUILDING_FILES = ['dock2', 'mine2', 'forge2', 'farm2', 'windmill2', 'custom_house2',
  'lumber_hut2', 'sawmill2', 'temple2', 'temple2', 'temple2', 'temple2',
  'monument2', 'monument2', 'monument2', 'monument2', 'monument2', 'monument2', 'monument2'];
const UNIT_DIRS = ['warrior', 'rider', 'defender', 'swordsman', 'archer', 'catapult',
  'knight', 'mind_bender', 'boat', 'ship', 'battleship', 'superunit'];

function load(src) {
  return new Promise((resolve) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = () => resolve(null);   // missing variant -> fallback handled in lookup
    img.src = src;
  });
}

// The tile art carries a baked darker rim that reads as a grid between tiles.
// Crop a few pixels off every side and stretch back to full size: the rim is
// discarded and, because the scale is uniform about the center, features that
// cross tile edges (coastlines) still meet their neighbors.
function deborder(img, inset = 3) {
  if (!img) return img;
  const cv = document.createElement('canvas');
  cv.width = img.width;
  cv.height = img.height;
  cv.getContext('2d').drawImage(img,
    inset, inset, img.width - 2 * inset, img.height - 2 * inset,
    0, 0, img.width, img.height);
  return cv;
}

export class Assets {
  static async load(base = 'assets', onProgress = null) {
    const a = new Assets();
    // Preferred path: ONE atlas download, sprites sliced back out as
    // ImageBitmaps. Fallback: per-file loading via manifest.json.
    let atlas = null, frames = null;
    try {
      frames = await fetch(`${base}/atlas.json`).then((r) => r.ok ? r.json() : null);
      if (frames) {
        const blob = await fetch(`${base}/atlas.webp`).then((r) => r.ok ? r.blob() : null);
        if (blob) atlas = await createImageBitmap(blob);
        else frames = null;
      }
    } catch { frames = null; }
    // manifest of files that actually exist (written by web/build.sh), so we
    // never request the hundreds of optional variants that aren't there
    const have = frames
      ? new Set(Object.keys(frames))
      : new Set(await fetch(`${base}/manifest.json`).then((r) => r.ok ? r.json() : []).catch(() => []));
    const jobs = [];
    const fetchOne = (rel, src) => {
      if (atlas) {
        const [x, y, w, h] = frames[rel];
        return createImageBitmap(atlas, x, y, w, h);
      }
      return load(src);
    };
    const put = (obj, key, src) => {
      const rel = src.slice(base.length + 1);
      if (have.size && !have.has(rel)) return;
      jobs.push(fetchOne(rel, src).then((img) => { obj[key] = img; }));
    };

    const putTile = (obj, key, src) => {
      const rel = src.slice(base.length + 1);
      if (have.size && !have.has(rel)) return;
      jobs.push(fetchOne(rel, src).then((img) => { obj[key] = deborder(img); }));
    };
    a.terrain = {};      // base tile per terrain id
    a.variants = {};     // `${terrainId}|${suffix}` -> image
    TERRAIN_BASE.forEach((f, i) => {
      putTile(a.terrain, i, `${base}/terrain/${f}.png`);
      for (const v of TERRAIN_VARIANTS)
        putTile(a.variants, `${i}|${v}`, `${base}/terrain/${f}-${v}.png`);
    });

    a.resource = {};
    RESOURCE_FILES.forEach((f, i) => { if (f) put(a.resource, i, `${base}/resource/${f}.png`); });
    a.building = {};
    BUILDING_FILES.forEach((f, i) => put(a.building, i, `${base}/building/${f}.png`));

    a.unit = UNIT_DIRS.map(() => ({}));
    a.unitExhausted = UNIT_DIRS.map(() => ({}));
    UNIT_DIRS.forEach((dir, type) => {
      for (let tribe = 0; tribe < 12; tribe++) {
        put(a.unit[type], tribe, `${base}/unit/${dir}/${tribe}.png`);
        put(a.unitExhausted[type], tribe, `${base}/unit/${dir}/${tribe}Exhausted.png`);
      }
    });

    a.misc = {};
    put(a.misc, 'shine', `${base}/shine3.png`);
    put(a.misc, 'walls', `${base}/terrain/walls.png`);
    put(a.misc, 'roadV', `${base}/terrain/road-v-half.png`);
    put(a.misc, 'roadD', `${base}/terrain/road-d-half.png`);
    let done = 0;
    await Promise.all(jobs.map((j) => j.then(() => { done++; onProgress?.(done / jobs.length, done, jobs.length); })));
    return a;
  }

  // terrain image for id `t` with optional edge-variant suffix
  terrainVariant(t, suffix) {
    if (suffix) {
      const img = this.variants[`${t}|${suffix}`];
      if (img) return img;
    }
    return this.terrain[t];
  }

  unitSprite(type, tribe, exhausted) {
    const bank = exhausted ? this.unitExhausted : this.unit;
    return bank[type][tribe] || this.unit[type][tribe] || null;
  }
}
