// polytopia-rl website: two modes.
//   Agents — agents play each other; speed slider from slow-mo to unthrottled.
//   Human  — you are red (player 0); agents drive the rest.
import { Game, verbName, V } from './game.js';
import { Renderer, PLAYER_COLOR } from './render.js';
import { RandomAgent } from './agent.js';

const $ = (id) => document.getElementById(id);

const state = {
  mode: 'agents',          // 'agents' | 'human'
  paused: false,
  speed: 35,               // slider 0..100; 100 = unthrottled
  stepAccum: 0,
  bannerUntil: 0,
};

const game = await Game.load();
const renderer = new Renderer($('board'), game);
const agent = new RandomAgent();
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

function legalTiles() {
  const m = game.mask(0), out = [];
  for (let t = 0; t < game.tiles; t++) if (m[t]) out.push(t);
  return out;
}

function showVerbs() {
  const box = $('verbs'), btns = $('verb-buttons');
  btns.innerHTML = '';
  if (!humanTurn()) { box.classList.add('hidden'); return; }
  const m = game.mask(0);
  let any = false;
  for (let v = 0; v < V.N; v++) {
    if (!m[game.tiles + v]) continue;
    any = true;
    const b = document.createElement('button');
    b.textContent = verbName(v);
    b.onclick = () => { game.act(game.tiles + v); afterAction(); };
    btns.appendChild(b);
  }
  box.classList.toggle('hidden', !any);
}

$('board').addEventListener('click', (ev) => {
  if (!humanTurn()) return;
  const rect = ev.target.getBoundingClientRect();
  const scale = $('board').width / rect.width;
  const ts = renderer.tileSize();
  const x = Math.floor(((ev.clientX - rect.left) * scale) / ts);
  const y = Math.floor(((ev.clientY - rect.top) * scale) / ts);
  const t = y * game.size + x;
  if (t >= 0 && t < game.tiles && game.mask(0)[t]) { game.act(t); afterAction(); }
});

function afterAction() {
  if (game.gameOver()) showResult();
  refreshPanel();
  renderer.draw({ highlightTiles: humanTurn() ? legalTiles() : [] });
}

// --- panel ---
function refreshPanel() {
  const active = game.activePlayer();
  $('status').textContent =
    `turn ${game.tick()}  ·  ${state.mode === 'human' && active === 0 ? 'your move' : `player ${active + 1} thinking`}`;
  const box = $('players');
  box.innerHTML = '';
  for (let p = 0; p < game.players; p++) {
    const d = document.createElement('div');
    d.className = `p${p}` + (p === active ? ' active-turn' : '');
    d.textContent = `p${p + 1}  ★${game.stars(p)}  score ${game.score(p)}`;
    box.appendChild(d);
  }
  showVerbs();
  $('hint').textContent = humanTurn()
    ? 'click a highlighted tile, or pick an action on the right'
    : state.mode === 'human' ? '' : 'agents are playing';
}
function countBits(x) { let n = 0; while (x) { n += x & 1; x >>>= 1; } return n; }

function showResult() {
  const w = game.result(0) === 0 ? 0 : 1;
  const el = $('banner');
  el.textContent = state.mode === 'human'
    ? (w === 0 ? 'you win!' : 'you lose')
    : `player ${w + 1} wins`;
  el.style.color = PLAYER_COLOR[w];
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
      // unthrottled: sim for up to 12 ms per frame
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
  renderer.draw({ highlightTiles: humanTurn() ? legalTiles() : [] });
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
  game.newGame((Math.random() * 2 ** 31) | 0);
  $('banner').classList.add('hidden');
}
$('new-game').onclick = () => { game.newGame((Math.random() * 2 ** 31) | 0); $('banner').classList.add('hidden'); };
$('pause').onclick = () => { state.paused = !state.paused; $('pause').textContent = state.paused ? 'resume' : 'pause'; };
const speedInput = $('speed');
speedInput.oninput = () => { state.speed = +speedInput.value; $('speed-label').textContent = speedLabel(); };
speedInput.oninput();

requestAnimationFrame(frame);
