// polytopia-rl website: two modes.
//   Agents — agents play each other; speed slider from slow-mo to unthrottled.
//   Human  — you are red (player 0); agents drive the rest.
import { Game, verbName, V, TECHS } from './game.js';
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
  viewpoint: -2,           // Agents mode: -2 = follow active agent's fog (default),
                           // -1 = whole world, 0..N-1 = a fixed agent's fog
};

const assets = await Assets.load();
let game, renderer, agent;

async function loadEngine(players) {
  game = await Game.load(players);
  renderer = new Renderer($('board'), game, assets);
  const trained = await game.loadWeights(players);
  agent = trained ? new TrainedAgent() : new RandomAgent();
  document.title = trained ? 'polytopia-rl' : 'polytopia-rl (random agents)';
  game.newGame((Math.random() * 2 ** 31) | 0);
  state.panelSig = '';
  tally.wins.fill(0); tally.draws = 0;
  $('banner').classList.add('hidden');
  buildPlayersSeg();
  buildViewSeg();
}

function buildPlayersSeg() {
  const seg = $('players-seg');
  seg.innerHTML = '';
  for (const n of [2, 3, 4]) {
    const b = document.createElement('button');
    b.className = 'seg-btn' + (state.players === n ? ' active' : '');
    b.textContent = `${n}`;
    b.onclick = async () => {
      if (state.players === n) return;
      state.players = n;
      await loadEngine(n);
      updateScoreboard();
    };
    seg.appendChild(b);
  }
}

state.players = 2;   // engine loads at the bottom, after all declarations

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
// Human mode renders from the human's perspective; Agents mode renders the
// selected viewpoint: follow = the ACTIVE agent's fog, alternating with turns.
function viewer() {
  if (state.mode === 'human') return 0;
  if (state.viewpoint === -2) return game.activePlayer();
  return state.viewpoint;
}

function buildViewSeg() {
  const seg = $('view-seg');
  seg.innerHTML = '';
  const opts = [{ v: -2, label: 'follow' }, { v: -1, label: 'world' }];
  for (let p = 0; p < game.players; p++) opts.push({ v: p, label: `p${p + 1}` });
  for (const o of opts) {
    const b = document.createElement('button');
    b.className = 'seg-btn' + (state.viewpoint === o.v ? ' active' : '');
    if (o.v >= 0) b.style.setProperty('--seg-accent', PLAYER_COLOR[o.v]);
    b.textContent = o.label;
    b.onclick = () => { state.viewpoint = o.v; buildViewSeg(); };
    seg.appendChild(b);
  }
}

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
    let label = verbName(v);
    if (v >= V.RESEARCH0 && v < V.RESEARCH0 + 24)
      label += ` ★${game.techCost(0, v - V.RESEARCH0)}`;
    b.textContent = label;
    b.onclick = () => { game.act(game.tiles + v); if (game.gameOver()) showResult(); };
    btns.appendChild(b);
  }
  box.classList.toggle('hidden', !any);
}

// player panel: stable DOM built once per game (so the tech-tree <details>
// expansion state survives); values updated in place every frame
const playerEls = [];
// tech tiers for the tree layout (indices into TECHS)
const TECH_TIERS = [
  [0, 1, 2, 3, 4],                          // climbing fishing hunting organization riding
  [5, 6, 7, 8, 9, 10, 11, 12, 13, 14],      // tier 2
  [15, 16, 17, 18, 19, 20, 21, 22, 23],     // tier 3
];

function buildPlayersPanel() {
  const box = $('players');
  box.innerHTML = '';
  playerEls.length = 0;
  for (let p = 0; p < game.players; p++) {
    const d = document.createElement('div');
    d.className = `player-card p${p}`;
    const head = document.createElement('div');
    head.className = 'player-head';
    const det = document.createElement('details');
    const sum = document.createElement('summary');
    sum.textContent = 'tech tree';
    det.appendChild(sum);
    const tree = document.createElement('div');
    tree.className = 'tech-tree';
    const chips = [];
    for (const tier of TECH_TIERS) {
      const row = document.createElement('div');
      row.className = 'tech-row';
      for (const t of tier) {
        const c = document.createElement('span');
        c.className = 'tech-chip';
        c.textContent = TECHS[t];
        row.appendChild(c);
        chips[t] = c;
      }
      tree.appendChild(row);
    }
    det.appendChild(tree);
    d.appendChild(head);
    d.appendChild(det);
    box.appendChild(d);
    playerEls.push({ card: d, head, chips });
  }
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
  if (playerEls.length !== game.players) buildPlayersPanel();
  for (let p = 0; p < game.players; p++) {
    const el = playerEls[p];
    el.card.classList.toggle('active-turn', p === active);
    el.head.textContent =
      `p${p + 1}  ★${game.stars(p)}  +${game.income(p)}/turn  ·  score ${game.score(p)}`;
    const bits = game.techs(p) >>> 0;
    for (let t = 0; t < 24; t++)
      el.chips[t].classList.toggle('researched', ((bits >> t) & 1) === 1);
  }
  $('hint').textContent = humanTurn() ? PHASE_HINT[game.phase()] :
    state.mode === 'human' ? '' : 'agents are playing';
}

const tally = { wins: [0, 0, 0, 0], draws: 0 };

function showResult() {
  const el = $('banner');
  let w = -1;
  for (let p = 0; p < game.players; p++) if (game.result(p) === 0) w = p;
  if (w < 0) {
    tally.draws++;
    el.textContent = 'draw';
    el.style.background = 'rgba(31,36,48,.88)';
  } else {
    tally.wins[w]++;
    el.textContent = state.mode === 'human'
      ? (w === 0 ? 'you win!' : 'you lose')
      : `p${w + 1} wins · turn ${lastTick}`;
    el.style.background = PLAYER_COLOR[w];
  }
  el.classList.remove('hidden');
  // toast fades fast at high speed so it never obscures the next game
  const dur = actionsPerSecond() === Infinity ? 700 : Math.max(900, 2500 - state.speed * 18);
  state.bannerUntil = performance.now() + dur;
  updateScoreboard();
}

function updateScoreboard() {
  const sb = $('scoreboard');
  const total = tally.wins.reduce((a, b) => a + b, 0) + tally.draws;
  if (total === 0) { sb.classList.add('hidden'); return; }
  sb.classList.remove('hidden');
  sb.innerHTML = '';
  for (let p = 0; p < game.players; p++) {
    const s = document.createElement('span');
    s.className = 'w';
    s.style.color = PLAYER_COLOR[p];
    s.textContent = `${tally.wins[p]}`;
    sb.appendChild(s);
    if (p < game.players - 1) sb.appendChild(document.createTextNode('–'));
  }
  const d = document.createElement('span');
  d.className = 'draws';
  d.textContent = `${tally.draws} draws · ${total} games`;
  sb.appendChild(d);
}

// --- main loop ---
let last = performance.now();
let lastPanel = 0;
let lastTick = 0;
function frame(now) {
  const dt = Math.min(now - last, 100) / 1000;
  last = now;

  if (!state.paused && !humanTurn()) {
    const aps = actionsPerSecond();
    if (aps === Infinity) {
      // unthrottled: spend most of the frame simulating, paint at ~30fps
      const budget = performance.now() + 28;
      while (performance.now() < budget) {
        stepAgentOnce();
        if (humanTurn()) break;
      }
    } else {
      state.stepAccum += aps * dt;
      // clamp the backlog so a slow frame never causes a huge catch-up burst
      state.stepAccum = Math.min(state.stepAccum, 400);
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
  if (now - lastPanel > 100 || humanTurn()) {   // panel at 10Hz; instant for humans
    lastPanel = now;
    refreshPanel();
  }
  renderer.draw({ highlightTiles: humanTurn() ? legalTiles() : null, viewer: viewer() });
  requestAnimationFrame(frame);
}

function stepAgentOnce() {
  if (game.gameOver()) return;
  const p = game.activePlayer();
  if (state.mode === 'human' && p === 0) return;
  lastTick = game.tick();
  const a = agent.pick(game, p);
  if (a === null) return;
  const running = game.act(a);
  if (!running) showResult();   // env auto-resets; toast + scoreboard record it
}

// --- controls ---
$('mode-agents').onclick = () => setMode('agents');
$('mode-human').onclick = () => setMode('human');
function setMode(m) {
  state.mode = m;
  $('mode-agents').classList.toggle('active', m === 'agents');
  $('mode-human').classList.toggle('active', m === 'human');
  $('speed-row').classList.toggle('hidden', m === 'human');
  $('view-row').classList.toggle('hidden', m === 'human');
  game.newGame((Math.random() * 2 ** 31) | 0);
  $('banner').classList.add('hidden');
  state.panelSig = '';
  buildViewSeg();
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

// --- bootstrap: all declarations above are live now ---
await loadEngine(state.players);
requestAnimationFrame(frame);

// debug/test handle (also handy in the browser console)
window.__poly = {
  get game() { return game; }, get renderer() { return renderer; },
  state, humanTurn, legalTiles, setMode,
};
