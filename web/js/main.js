// polytopia-rl website: two modes.
//   Agents — agents play each other; speed slider from slow-mo to unthrottled.
//   Human  — you are red (player 0); agents drive the rest.
import { Game, verbName, V, TECHS } from './game.js';
import { Renderer, PLAYER_COLOR } from './render.js';
import { Assets } from './assets.js';
import { RandomAgent, TrainedAgent } from './agent.js';

const $ = (id) => document.getElementById(id);

// --- loading screen: real progress across sprites -> engine -> weights ---
function setLoad(label, frac) {
  $('loader-label').textContent = label;
  $('loader-pct').textContent = `${Math.round(frac * 100)}%`;
  $('loader-fill').style.transform = `scaleX(${frac})`;
}
function showLoader() { $('loader').classList.remove('is-out'); }
function hideLoader() { setLoad('ready', 1); $('loader').classList.add('is-out'); }

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

const assets = await Assets.load('assets', (f) => setLoad('sprites', f * 0.35));
let game, renderer, agent;

async function loadEngine(players) {
  state.loading = true;   // freeze the sim loop while the engine swaps out
  showLoader();
  setLoad('engine', 0.38);
  game = await Game.load(players);
  // observe every applied action (agent AND human) for the shared log
  const rawAct = game.act;
  game.act = (a) => { logAction(game.activePlayer(), a, game.phase()); return rawAct(a); };
  renderer = new Renderer($('board'), game, assets);
  const trained = await game.loadWeights(players, (f) => setLoad('policy weights', 0.42 + f * 0.58));
  agent = trained ? new TrainedAgent() : new RandomAgent();
  document.title = trained ? 'tribes-rl' : 'tribes-rl (random agents)';
  game.newGame((Math.random() * 2 ** 31) | 0);
  state.stepAccum = 0;
  state.panelSig = '';
  tally.wins.fill(0); tally.draws = 0;
  clearLog();
  $('banner').classList.add('hidden');
  buildPlayersSeg();
  buildViewSeg();
  hideLoader();
  state.loading = false;
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

// --- shared action log: every applied action (agent or human) becomes a line.
// Buffered here and flushed to the DOM with the panel (10Hz), so max-speed
// play never thrashes layout. Tile SELECTs are noise and are skipped.
const LOG_CAP = 80;
const logBuf = [];
let logDirty = false;
let logPendingVerb = null;   // verb name awaiting its TARGET tile
function pushLog(html) {
  logBuf.push(html);
  if (logBuf.length > LOG_CAP) logBuf.shift();
  logDirty = true;
}
// verbs that go through a TARGET phase (mirrors verb_needs_target in the env)
function verbNeedsTarget(v) {
  return v === V.BUILD_ROAD || (v >= V.BUILD0 && v < V.BUILD0 + 19) ||
    (v >= V.GATHER && v <= V.DESTROY) ||
    v === V.UNIT0 || v === V.UNIT0 + 1 || v === V.UNIT0 + 5;   // move, attack, convert
}
function logAction(p, a, phase) {
  const tag = `<b class="lp${p}">p${p + 1}</b>`;
  if (a >= game.tiles) {
    const v = a - game.tiles;
    if (v === V.END_TURN && phase === 0) {
      pushLog(`${tag} end turn <span class="turn">· t${game.tick() + 1}</span>`);
      return;
    }
    const name = verbName(v);
    if (verbNeedsTarget(v)) { logPendingVerb = name; return; }
    pushLog(`${tag} ${name}`);
  } else if (phase === 2) {   // PH_TARGET: tile completes the pending verb
    pushLog(`${tag} ${logPendingVerb || 'action'} → ${a % game.size},${(a / game.size) | 0}`);
    logPendingVerb = null;
  }
}
function flushLog() {
  if (!logDirty) return;
  logDirty = false;
  const el = $('log-lines');
  // don't yank the view away from someone reading scrolled-back history
  const follow = el.scrollHeight - el.scrollTop - el.clientHeight < 30;
  el.innerHTML = logBuf.join('<br>');
  if (follow) el.scrollTop = el.scrollHeight;
}
function clearLog() {
  logBuf.length = 0;
  logPendingVerb = null;
  logDirty = true;
}

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
    const out = game.eliminated(p) === 1;
    el.card.classList.toggle('active-turn', !out && p === active);
    el.card.classList.toggle('eliminated', out);
    el.head.textContent = out ?
      `p${p + 1}  eliminated` :
      `p${p + 1}  ★${game.stars(p)}  +${game.income(p)}/turn  ·  score ${game.score(p)}`;
    const bits = game.techs(p) >>> 0;
    for (let t = 0; t < 24; t++)
      el.chips[t].classList.toggle('researched', ((bits >> t) & 1) === 1);
  }
  $('hint').textContent = humanTurn() ? PHASE_HINT[game.phase()] : '';
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

  if (!state.paused && !state.loading && !humanTurn()) {
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
      // never queue more than ~2 frames of work
      state.stepAccum = Math.min(state.stepAccum, aps * 0.04 + 1);
      // time-budgeted like max speed: each step is a full policy forward
      // pass, so an unbudgeted catch-up burst freezes the UI at high rates
      const budget = performance.now() + 20;
      while (state.stepAccum >= 1) {
        state.stepAccum -= 1;
        stepAgentOnce();
        if (humanTurn()) break;
        if (performance.now() >= budget) { state.stepAccum = 0; break; }
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
    flushLog();
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
// fresh game + cleared sim backlog: mode/players/speed changes all restart
function resetGame() {
  game.newGame((Math.random() * 2 ** 31) | 0);
  state.stepAccum = 0;
  state.panelSig = '';
  clearLog();
  $('banner').classList.add('hidden');
}
$('mode-agents').onclick = () => setMode('agents');
$('mode-human').onclick = () => setMode('human');
function setMode(m) {
  state.mode = m;
  $('mode-agents').classList.toggle('active', m === 'agents');
  $('mode-human').classList.toggle('active', m === 'human');
  $('speed-row').classList.toggle('hidden', m === 'human');
  $('view-row').classList.toggle('hidden', m === 'human');
  // only show controls that exist in this mode
  $('end-turn').classList.toggle('hidden', m !== 'human');
  $('pause').classList.toggle('hidden', m === 'human');
  resetGame();
  buildViewSeg();
}
$('new-game').onclick = () => resetGame();
$('pause').onclick = () => { state.paused = !state.paused; $('pause').textContent = state.paused ? 'resume' : 'pause'; };
$('end-turn').onclick = () => {
  if (humanTurn() && game.mask(0)[game.tiles + V.END_TURN]) {
    game.act(game.tiles + V.END_TURN);
    if (game.gameOver()) showResult();
  }
};
const speedInput = $('speed');
speedInput.oninput = () => { state.speed = +speedInput.value; $('speed-label').textContent = speedLabel(); };
// on release: drop any sim backlog so the new pace applies cleanly (the
// game itself keeps running)
speedInput.onchange = () => { state.stepAccum = 0; };
speedInput.oninput();

// --- theme: system preference by default; the toggle pins light/dark ---
const themeQuery = matchMedia('(prefers-color-scheme: dark)');
const ICON_MOON = '<svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.8A9 9 0 1 1 11.2 3 7 7 0 0 0 21 12.8z"/></svg>';
const ICON_SUN = '<svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>';
function isDark() {
  const t = document.documentElement.dataset.theme;
  return t ? t === 'dark' : themeQuery.matches;
}
function applyTheme(pin) {   // pin = 'light' | 'dark' | null (follow system)
  if (pin) { document.documentElement.dataset.theme = pin; localStorage.setItem('theme', pin); }
  else { delete document.documentElement.dataset.theme; localStorage.removeItem('theme'); }
  $('theme').innerHTML = isDark() ? ICON_SUN : ICON_MOON;   // icon = what it switches to
  if (renderer) renderer.refreshTheme();
}
$('theme').onclick = () => applyTheme(isDark() ? 'light' : 'dark');
themeQuery.onchange = () => applyTheme(document.documentElement.dataset.theme || null);
applyTheme(localStorage.getItem('theme'));

// --- bootstrap: all declarations above are live now ---
await loadEngine(state.players);
requestAnimationFrame(frame);
// warm the 4p weights into the HTTP cache once the first game is running
setTimeout(() => { fetch('weights/latest_4p.bin').catch(() => {}); }, 10000);

// debug/test handle (also handy in the browser console)
window.__poly = {
  get game() { return game; }, get renderer() { return renderer; },
  state, humanTurn, legalTiles, setMode,
};
