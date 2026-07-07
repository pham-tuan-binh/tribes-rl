// Sprite loading — asset paths mirror the Tribes reference GUI
// (reference/Tribes/src/core/Types.java imageFile mappings).
const TERRAIN_FILES = ['plain', 'water', 'deepwater', 'mountain3', 'village2', 'city3', 'forest2'];
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
    img.onerror = () => resolve(null);   // missing sprite -> draw nothing
    img.src = src;
  });
}

export class Assets {
  static async load(base = 'assets') {
    const a = new Assets();
    const jobs = [];
    const put = (obj, key, src) => jobs.push(load(src).then((img) => { obj[key] = img; }));

    a.terrain = {};
    TERRAIN_FILES.forEach((f, i) => put(a.terrain, i, `${base}/terrain/${f}.png`));
    a.resource = {};
    RESOURCE_FILES.forEach((f, i) => { if (f) put(a.resource, i, `${base}/resource/${f}.png`); });
    a.building = {};
    BUILDING_FILES.forEach((f, i) => put(a.building, i, `${base}/building/${f}.png`));

    // units: [type][tribeKey] and exhausted variants
    a.unit = UNIT_DIRS.map(() => ({}));
    a.unitExhausted = UNIT_DIRS.map(() => ({}));
    UNIT_DIRS.forEach((dir, type) => {
      for (let tribe = 0; tribe < 12; tribe++) {
        put(a.unit[type], tribe, `${base}/unit/${dir}/${tribe}.png`);
        put(a.unitExhausted[type], tribe, `${base}/unit/${dir}/${tribe}Exhausted.png`);
      }
    });

    a.misc = {};
    put(a.misc, 'fog', `${base}/fog.png`);
    put(a.misc, 'shine', `${base}/shine3.png`);
    put(a.misc, 'walls', `${base}/terrain/walls.png`);
    put(a.misc, 'road', `${base}/terrain/road.png`);
    await Promise.all(jobs);
    return a;
  }

  unitSprite(type, tribe, exhausted) {
    const bank = exhausted ? this.unitExhausted : this.unit;
    return bank[type][tribe] || this.unit[type][tribe] || null;
  }
}
