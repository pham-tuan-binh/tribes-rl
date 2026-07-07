// polytopia-rl website: two modes.
//   Agents — agents play each other; speed slider from slow-mo to unthrottled.
//   Human  — you are red (player 0); agents drive the rest.
import { Game, verbName, V } from './game.js';
import { Renderer, PLAYER_COLOR } from './render.js';
import { Assets } from './assets.js';
import { RandomAgent, TrainedAgent } from './agent.js';

const $ = (id) => document.getElementById(id);

const state = {
  mode: 'agents',          // 'agents' | 'human'
  paused: false,
  speed: 35,               // slider 0..100; 100 = unthrottled
  stepAccum: 0,
  bannerUntil: 0,
  panelSig: '',            // rebuild the side panel only when this changes
};

const [game, assets] = await Promise.all([Game.load(), Assets.load()]);
const renderer = new Renderer($('board'), game, assets);
const trained = await game.loadWeights('weights/latest.bin');
const agent = trained ? new TrainedAgent() : new RandomAgent();
document.title = trained ? 'polytopia-rl' : 'polytopia-rl (random agents)';
game.newGame((Math.random() * 2 ** 31) | 0);

// --- speed mapping: 0 -> 0.5 actions/s ... 100 -> unthrottled ---
function actionsPerSecond() {
  if (state.speed >= 100) return Infinity;
  return 0.5 * Math.pow(1.12, state.speed);   // ~0.5 .. ~40k
}
function speedLabel() {
  const a = actionsPerSecond();
  return a === Infinity ? 'max' : a < 10 ? `${a.toFixed(1)}/s` : `${Math.round(a)}/s`;
}

// --- human interaction ---
function humanTurn() { return state.mode === 'human' && game.activePlayer() === 0 && !game.gameOver(); }
function viewer() { return state.mode === 'human' ? 0 : -1; }

function legalTiles() {
  const m = game.mask(0), out = new Set();
  for (let t = 0; t < game.tiles; t++) if (m[t]) out.add(t);
  return out;
}

$('board').addEventListener('click', (ev) => {
  if (!humanTurn()) return;
  const rect = ev.target.getBoundingClientRect();
  const scale = $('board').width / rect.width;
  const { col, row } = renderer.unproject((ev.clientX - rect.left) * scale,
                                          (ev.clientY - rect.top) * scale);
  if (col < 0 || row < 0 || col >= game.size || row >= game.size) {
    if (game.phase() > 0) game.cancel();   // click off-board = cancel
    return;
  }
  const t = row * game.size + col;
  if (game.mask(0)[t]) game.act(t);
  else if (game.phase() > 0) game.cancel(); // illegal tile while selecting = cancel
  if (game.gameOver()) showResult();
});
window.addEventListener('keydown', (ev) => {
  if (ev.key === 'Escape' && humanTurn() && game.phase() > 0) game.cancel();
});

// --- side panel (rebuilt only when the situation changes) ---
const PHASE_HINT = [
  'select a unit or city (glowing tiles), research, or end your turn',
  'choose what the selected piece does — Esc to cancel',
  'choose a target tile (glowing) — Esc to cancel',
];

function panelSignature() {
  if (!humanTurn()) return `off|${game.activePlayer()}|${game.tick()}`;
  const m = game.mask(0);
  let verbs = '';
  for (let v = 0; v < V.N; v++) if (m[game.tiles + v]) verbs += v + ',';
  return `on|${game.phase()}|${game.selectedTile()}|${verbs}`;
}

function rebuildPanel() {
  const btns = $('verb-buttons');
  btns.innerHTML = '';
  const box = $('verbs');
  if (!humanTurn()) { box.classList.add('hidden'); $('end-turn').disabled = true; return; }
  const m = game.mask(0);
  $('end-turn').disabled = !m[game.tiles + V.END_TURN];
  let any = false;
  for (let v = 0; v < V.N; v++) {
    if (v === V.END_TURN || !m[game.tiles + v]) continue;
    any = true;
    const b = document.createElement('button');
    b.textContent = verbName(v);
    b.onclick = () => { game.act(game.tiles + v); if (game.gameOver()) showResult(); };
    btns.appendChild(b);
  }
  box.classList.toggle('hidden', !any);
}

function refreshPanel() {
  const sig = panelSignature();
  if (sig !== state.panelSig) {
    state.panelSig = sig;
    rebuildPanel();
  }
  const active = game.activePlayer();
  $('status').textContent =
    `turn ${game.tick()}  ·  ${humanTurn() ? 'YOUR TURN' : `player ${active + 1} thinking`}`;
  const box = $('players');
  box.innerHTML = '';
  for (let p = 0; p < game.players; p++) {
    const d = document.createElement('div');
    d.className = `p${p}` + (p === active ? ' active-turn' : '');
    d.textContent = `p${p + 1}  ★${game.stars(p)}  score ${game.score(p)}`;
    box.appendChild(d);
  }
  $('hint').textContent = humanTurn() ? PHASE_HINT[game.phase()] :
    state.mode === 'human' ? '' : 'agents are playing';
}

function showResult() {
  const el = $('banner');
  if (game.result(0) === 2) {
    el.textContent = 'draw — no capital conquest';
    el.style.color = '#dfe6ee';
  } else {
    const w = game.result(0) === 0 ? 0 : 1;
    el.textContent = state.mode === 'human'
      ? (w === 0 ? 'you win!' : 'you lose')
      : `player ${w + 1} wins`;
    el.style.color = PLAYER_COLOR[w];
  }
  el.classList.remove('hidden');
  state.bannerUntil = performance.now() + 2500;
}

// --- main loop ---
let last = performance.now();
function frame(now) {
  const dt = Math.min(now - last, 100) / 1000;
  last = now;

  if (!state.paused && !humanTurn()) {
    const aps = actionsPerSecond();
    if (aps === Infinity) {
      const budget = performance.now() + 12;
      while (performance.now() < budget) {
        stepAgentOnce();
        if (humanTurn()) break;
      }
    } else {
      state.stepAccum += aps * dt;
      while (state.stepAccum >= 1) {
        state.stepAccum -= 1;
        stepAgentOnce();
        if (humanTurn()) break;
      }
    }
  }

  if (state.bannerUntil && now > state.bannerUntil) {
    $('banner').classList.add('hidden');
    state.bannerUntil = 0;
  }
  refreshPanel();
  renderer.draw({ highlightTiles: humanTurn() ? legalTiles() : null, viewer: viewer() });
  requestAnimationFrame(frame);
}

function stepAgentOnce() {
  if (game.gameOver()) return;
  const p = game.activePlayer();
  if (state.mode === 'human' && p === 0) return;
  const a = agent.pick(game, p);
  if (a === null) return;
  const running = game.act(a);
  if (!running) showResult();   // env auto-resets; banner shows the outcome
}

// --- controls ---
$('mode-agents').onclick = () => setMode('agents');
$('mode-human').onclick = () => setMode('human');
function setMode(m) {
  state.mode = m;
  $('mode-agents').classList.toggle('active', m === 'agents');
  $('mode-human').classList.toggle('active', m === 'human');
  $('speed-row').classList.toggle('hidden', m === 'human');
  game.newGame((Math.random() * 2 ** 31) | 0);
  $('banner').classList.add('hidden');
  state.panelSig = '';
}
$('new-game').onclick = () => { game.newGame((Math.random() * 2 ** 31) | 0); $('banner').classList.add('hidden'); state.panelSig = ''; };
$('pause').onclick = () => { state.paused = !state.paused; $('pause').textContent = state.paused ? 'resume' : 'pause'; };
$('end-turn').onclick = () => {
  if (humanTurn() && game.mask(0)[game.tiles + V.END_TURN]) {
    game.act(game.tiles + V.END_TURN);
    if (game.gameOver()) showResult();
  }
};
const speedInput = $('speed');
speedInput.oninput = () => { state.speed = +speedInput.value; $('speed-label').textContent = speedLabel(); };
speedInput.oninput();

requestAnimationFrame(frame);

// debug/test handle (also handy in the browser console)
window.__poly = { game, renderer, state, humanTurn, legalTiles, setMode };
