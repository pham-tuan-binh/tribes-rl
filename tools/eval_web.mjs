// Headless eval of a checkpoint in the ACTUAL web engine (dist/poly.js):
// N games per player count, temperature-0.5 sampling, fog on.
//   node tools/eval_web.mjs [weights.bin] [games-per-count]
import createPoly from '../web/dist/poly.js';
import { readFileSync } from 'fs';

const weights = process.argv[2] || 'web/weights/latest.bin';
const N = +(process.argv[3] || 12);
const m = await createPoly();
const f = (n, r, a) => m.cwrap(n, r, a);
const data = readFileSync(weights);
const ptr = f('poly_weights_alloc', 'number', ['number'])(data.length);
m.HEAPU8.set(new Uint8Array(data), ptr);
if (f('poly_weights_load', 'number', ['number'])(data.length) !== 1) {
  console.error('weights refused (arch mismatch with this build)');
  process.exit(1);
}
const act = f('poly_act', 'number', ['number']);
const agentAct = f('poly_agent_act', 'number', ['number']);
const active = f('poly_active_player', 'number', []);
const tick = f('poly_tick', 'number', []);
const result = f('poly_result', 'number', ['number']);
const newGame = f('poly_new_game', null, ['number', 'number', 'number']);

for (const np of [2, 3, 4]) {
  const turns = [], seats = [0, 0, 0, 0];
  let draws = 0;
  for (let g = 0; g < N; g++) {
    newGame(9000 + g * 13 + np * 1000, np, 1);
    let steps = 0, maxT = 0;
    while (steps++ < 150000) {
      maxT = Math.max(maxT, tick());
      if (act(agentAct(active())) === 0) break;
    }
    const w = [...Array(np).keys()].find((p) => result(p) === 1);
    if (w === undefined) draws++;
    else { seats[w]++; turns.push(maxT + 1); }
  }
  turns.sort((a, b) => a - b);
  const avg = turns.length ? (turns.reduce((a, b) => a + b, 0) / turns.length).toFixed(1) : '-';
  console.log(`${np}p: ${N - draws}/${N} conquests, draws ${draws}, ` +
    `median ${turns[turns.length >> 1] ?? '-'} avg ${avg} turns, seats ${seats.slice(0, np).join('/')}`);
}
