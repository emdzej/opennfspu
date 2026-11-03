// OpenNFSPU in the browser: opennfspu.wasm in gasm's Worker mode. The worker runs the game and reads the data
// on demand, either from the site's OPFS copy (FileSystemSyncAccessHandle) or straight from the picked files
// (FileReaderSync). The game draws through gasm:gl (OpenGL ES 3.0 / WebGL 2, as the original draws through
// Direct3D 7): the page hands the worker its canvas (transferControlToOffscreen, gasm's `glCanvas`) and
// sends the display size with each batch, so the game renders at the size it is shown. The page keeps
// input and audio.
//
// Test and debug query parameters:
//   hashframes=N   run N frames on virtual time as fast as possible, then print the same
//                  "frames=... video_fnv32=... audio_fnv32=..." line as gasm-run --headless
//                  (globalThis.__opennfspuResult); in-memory storage, like gasm-run's headless runs
//   batch=N        with hashframes: frames per worker batch (default 250)
//   input=SCRIPT   with hashframes: scripted input, gasm-run's --input syntax ("60:KEY(Enter),...")
//   size=WxH       the GL drawing size instead of the display's (hash runs: 1280x720, as gasm-run --headless)
//   autoplay       start the imported data right away
//   any parameter the game reads (docs/guide/parameters.md) is passed through; relay=ws(s)://host:port
//                  (a gasm-relay, with room=) also lets the game connect to that host (gasm:net), and
//                  only that one, for network games
import { BrowserInput, ProcExit, Resampler } from './vendor/gasm/gasm-host.js';
import { GasmWorker } from './vendor/gasm/gasm-worker.js';
import { InputScript } from './vendor/gasm/input-script.js';
import * as data from './data.js';
import * as saves from './saves.js';

const $ = (id) => document.getElementById(id);
const WIDE_KEY = 'opennfspu.widescreen';
const query = new URLSearchParams(location.search);
const PAGE_PARAMS = new Set(['hashframes', 'batch', 'input', 'size', 'autoplay']);
const HASH_FRAMES = Number(query.get('hashframes') || 0);
const BATCH = Number(query.get('batch') || 250);   // hash runs: frames per worker batch
const FIXED_SIZE = /^(\d+)x(\d+)$/.exec(query.get('size') ?? '')?.slice(1).map(Number) ?? (HASH_FRAMES > 0 ? [1280, 720] : null);   // gasm-run --headless draws GL at 1280x720
const STORAGE = 'opennfspu';              // gasm:storage namespace (IndexedDB): profiles, options
const TITLE = 'Need for Speed: Porsche Unleashed';
const hasOpfs = !!navigator.storage?.getDirectory;
const hasGl = (() => { try { return !!document.createElement('canvas').getContext('webgl2'); } catch { return false; } })();
const hasOffscreen = typeof HTMLCanvasElement !== 'undefined' && 'transferControlToOffscreen' in HTMLCanvasElement.prototype;

function message(text, kind = 'info') {
  const m = $('message');
  m.hidden = !text;
  m.textContent = text ?? '';
  m.className = `message ${kind}`;
  if (text) console.log(`[play] ${text}`);
}
const show = (id, on) => { $(id).hidden = !on; };

// ---- the canvas -----------------------------------------------------------------------------
// A canvas can be transferred to a worker only once: every start gets a fresh one.
let canvas = $('screen');
function freshCanvas() {
  const c = document.createElement('canvas');
  c.id = 'screen';
  c.tabIndex = 0;
  c.setAttribute('aria-label', TITLE);
  c.ondblclick = () => $('fullscreen').onclick();
  canvas.replaceWith(c);
  canvas = c;
  keyboard.setElement(c);
  return c;
}
/** The drawing size in device pixels: the canvas as shown (it follows fullscreen and the window). */
const displaySize = () => FIXED_SIZE ?? [Math.max(1, Math.round(canvas.clientWidth * (devicePixelRatio || 1))) || 960,
                                         Math.max(1, Math.round(canvas.clientHeight * (devicePixelRatio || 1))) || 720];

// ---- keyboard and gamepads ------------------------------------------------------------------
// The game reads the raw keyboard itself (src/platform_gasm.c maps gasm's keys to the DirectInput codes the
// original reads), so the page only collects it. Gamepads are virtual pads 0-3, in connection order.
const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
const keyboard = new BrowserInput(canvas, { ignore: (e) => !running || typing(e) }).attach();
// While playing, keys shouldn't scroll the page or open the browser's menus (Alt, F10); browser shortcuts
// (Cmd/Ctrl + key) still work. A tap of Escape goes to the game; holding it for a second stops the game,
// like gasm-run.
const browserKey = (e) => e.metaKey || (e.ctrlKey && !/^(Control|Shift)/.test(e.code));
let escDown = 0;
addEventListener('keydown', (e) => {
  if (!running || typing(e)) return;
  if (e.code === 'Escape') { if (!e.repeat) escDown = performance.now(); return; }
  if (!browserKey(e)) e.preventDefault();
});
addEventListener('keyup', (e) => { if (e.code === 'Escape') escDown = 0; });
addEventListener('blur', () => { escDown = 0; });

// W3C "standard" gamepad mapping -> gasm button bit (A east, B south, X north, Y west, like gasm's app.js).
const PAD = { 1: 0, 0: 1, 3: 2, 2: 3, 4: 4, 5: 5, 6: 4, 7: 5, 8: 6, 9: 7, 12: 8, 13: 9, 14: 10, 15: 11 };
function readPads() {
  const pads = [0, 0, 0, 0];
  let n = 0;
  for (const gp of navigator.getGamepads?.() ?? []) {
    if (!gp || n > 3) continue;
    let m = 0;
    for (const [btn, bit] of Object.entries(PAD)) if (gp.buttons[btn]?.pressed) m |= 1 << bit;
    const [x = 0, y = 0] = gp.axes;
    if (x < -0.5) m |= 1 << 10; if (x > 0.5) m |= 1 << 11;
    if (y < -0.5) m |= 1 << 8; if (y > 0.5) m |= 1 << 9;
    pads[n++] |= m;
  }
  return pads;
}

// ---- audio: the gasm web player's AudioWorklet queue ------------------------------------------
const WORKLET = `
class GasmOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.len = 0; this.primed = false;
    this.target = Math.round(sampleRate * 0.06) * 2; this.max = Math.round(sampleRate * 0.2) * 2;
    this.port.onmessage = (e) => {
      this.q.push(e.data); this.len += e.data.length;
      while (this.len > this.max && this.q.length > 1) { this.len -= this.q[0].length - this.off; this.q.shift(); this.off = 0; }
    };
  }
  process(_, [out]) {
    const L = out[0], R = out[1] ?? out[0];
    if (!this.primed && this.len >= this.target) this.primed = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.primed || this.len < 2) { this.primed = false; L[i] = R[i] = 0; continue; }
      const b = this.q[0];
      L[i] = b[this.off]; R[i] = b[this.off + 1];
      this.off += 2; this.len -= 2;
      if (this.off >= b.length) { this.q.shift(); this.off = 0; }
    }
    return true;
  }
}
registerProcessor('gasm-out', GasmOut);`;

let audioCtx = null, audioNode = null, gain = null, resampler = null, audioReady = null;
let muted = localStorage.getItem('opennfspu.muted') === '1';
/** Call from a click handler (browsers only start audio on a user gesture). */
function startAudio() {
  if (!audioCtx) {
    try {
      audioCtx = new AudioContext({ latencyHint: 'interactive' });
    } catch (e) { message(`No sound: ${e.message}`, 'warn'); return; }
    audioReady = audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }))).then(() => {
      audioNode = new AudioWorkletNode(audioCtx, 'gasm-out', { outputChannelCount: [2] });
      gain = new GainNode(audioCtx, { gain: muted ? 0 : 1 });
      audioNode.connect(gain).connect(audioCtx.destination);
      resampler = new Resampler(audioCtx.sampleRate);
    }).catch((e) => { message(`No sound: ${e.message}`, 'warn'); audioCtx = null; });
  }
  audioCtx?.resume();
}
function onAudio(samples, rate, channels) {
  if (!audioNode) return;
  const out = resampler.process(samples, rate, channels);
  audioNode.port.postMessage(out.slice());
}
function setMuted(m) {
  muted = m;
  localStorage.setItem('opennfspu.muted', m ? '1' : '0');
  if (gain) gain.gain.value = m ? 0 : 1;
  $('sound').textContent = m ? 'Sound: off' : 'Sound: on';
}
$('sound').onclick = () => { startAudio(); setMuted(!muted); };
setMuted(muted);

$('fullscreen').onclick = () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else ($('stage').requestFullscreen ?? $('stage').webkitRequestFullscreen)?.call($('stage'));
};
canvas.ondblclick = () => $('fullscreen').onclick();
// gasm.set_fullscreen: honoured by gasm only shortly after a key press or click, as browsers require.
function gameFullscreen(on) {
  if (on && !document.fullscreenElement) $('stage').requestFullscreen?.().catch(() => {});
  else if (!on && document.fullscreenElement) document.exitFullscreen().catch(() => {});
}
document.addEventListener('fullscreenchange', () => { if (worker) worker.fullscreen = !!document.fullscreenElement; });

// ---- running the game ---------------------------------------------------------------------
let worker = null, running = false, inflight = false, rafId = 0, wasmBytes = null, lastLog = '', wantPng = null;
let acc = 0, last = 0, fpsN = 0, fpsT = 0;

async function loadWasm() {
  if (!wasmBytes) {
    const r = await fetch(new URL('opennfspu.wasm', import.meta.url));
    if (!r.ok) throw new Error(`opennfspu.wasm: HTTP ${r.status}`);
    wasmBytes = await r.arrayBuffer();
  }
  return wasmBytes.slice(0); // start() transfers its copy to the worker
}

function onLog(msg) {
  console.log(msg);
  if (/^\[guest\]/.test(msg)) lastLog = msg.replace(/^\[guest\]\s*/, '');
}

async function stopGame() {
  cancelAnimationFrame(rafId);
  running = false; inflight = false; escDown = 0;
  const w = worker; worker = null;
  await w?.exit();          // flushes the saves, releases the OPFS handles
}

function stopped(e) {
  running = false;
  if (e instanceof ProcExit) message('The game has ended. Press Play to start again.');
  else message(`The game stopped: ${e.message}${lastLog ? ` (${lastLog})` : ''}`, 'error');
  stopGame();
  endGameView();
}

function endGameView() {
  cancelAnimationFrame(rafId);
  show('game', false);
  show('setup', true);
  refreshImported();
  refreshSaves();
}

/** Start the game from 'opfs' (the imported data) or a checked source (read in place). */
async function play(source) {
  startAudio();                        // still inside the click's user gesture
  message('');
  await stopGame();
  lastLog = '';
  if (!hasGl || !hasOffscreen) {
    message('This browser can\'t run OpenNFSPU: it needs WebGL 2 and OffscreenCanvas (a recent Chrome, Edge, Firefox or Safari 17 and later).', 'error');
    return;
  }
  show('game', true);
  show('setup', HASH_FRAMES > 0);      // keep the page visible in test runs
  $('status').textContent = 'Starting...';
  const params = Object.fromEntries([...query].filter(([k]) => !PAGE_PARAMS.has(k)));
  if (!('aspect' in params) && $('aspect').value) params.aspect = $('aspect').value;   // (the address wins)
  if (params.aspect === 'auto') params.aspect = screenShape() || undefined;
  if (!params.aspect) delete params.aspect;
  // The stage takes the game's shape: the races fill a widescreen one, the 4:3 menus have bars at the sides.
  $('stage').style.setProperty('--ar', { '16:9': 16 / 9, '16:10': 16 / 10 }[params.aspect] ?? 4 / 3);
  let allowNet = false;   // network games: the relay's host only
  try { if (params.relay) allowNet = [new URL(params.relay).hostname]; } catch { /* not a URL: no network */ }
  const c = freshCanvas();
  try {
    await audioReady;
    worker = await GasmWorker.start({
      wasm: await loadWasm(), assets: data.assetSpecs(source), params, allowNet,
      storage: HASH_FRAMES > 0 ? null : STORAGE,     // hash runs: in memory, like gasm-run --headless
      hashing: HASH_FRAMES > 0, virtualTime: HASH_FRAMES > 0, onLog, onAudio, keyboard: true,
      glCanvas: c.transferControlToOffscreen(), size: displaySize(),
      onTitle: (t) => { document.title = `${t ?? TITLE} | OpenNFSPU`; },
      onFullscreen: gameFullscreen,
    });
  } catch (e) {
    worker = null;
    const busy = /NoModificationAllowed|InvalidState|lock|access handle/i.test(`${e.name} ${e.message}`);
    if (busy) message('The imported data is in use by another OpenNFSPU tab or window. Close it and try again.', 'error');
    else if (e instanceof ProcExit) message(`The game could not start${lastLog ? `: ${lastLog}` : ''}.${source !== 'opfs' && source.kind === 'image' ? ' A disc image must hold the installed game (GameData, FEData, Porsche.exe), not the setup disc.' : ''}`, 'error');
    else message(`The game could not start: ${e.message}`, 'error');
    endGameView();
    return;
  }
  document.title = `${TITLE} | OpenNFSPU`;
  if (HASH_FRAMES > 0) return hashRun(HASH_FRAMES);
  canvas.focus?.();
  acc = 0; last = performance.now(); running = true;
  rafId = requestAnimationFrame(tick);
}

function tick(now) {
  rafId = requestAnimationFrame(tick);
  if (!running || !worker) return;
  if (escDown && now - escDown >= 1000) {   // Escape held: stop (the tap already went to the game)
    $('stop').onclick();
    return;
  }
  // The game sets its frame rate (gasm.set_frame_rate): the worker reports it.
  const period = 1000 / worker.frameRate;
  acc += Math.min(now - last, 100);    // clamp after tab switches
  last = now;
  // Fixed timestep; when catching up (at most 4 frames), only the last one is shown.
  const due = Math.min(4, Math.floor(acc / period));
  if (!inflight && due > 0) {          // one batch in flight: the worker sets the pace
    const pads = readPads();
    inflight = true;
    acc -= due * period;
    const steps = Array.from({ length: due }, (_, k) => ({ pads, input: keyboard.frame(k === 0) }));
    const copy = !!wantPng;
    worker.frames(steps, true, { size: displaySize(), copy }).then((r) => {
      inflight = false;
      fpsN += due;
      if (copy) { wantPng(r.png ?? null); wantPng = null; }
      keyboard.setMode(worker?.inputMode ?? 0);
    }, stopped);
  }
  if (acc > period * 4) acc = 0;       // fell far behind: resync
  if (now - fpsT >= 1000) { $('status').textContent = `${fpsN} frames/s`; fpsN = 0; fpsT = now; }
}

/** The next frame the game draws, as a PNG Blob (tests; the WebGL canvas belongs to the worker). */
function framePng() { return new Promise((r) => { wantPng = r; }); }

const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
/** gasm-run --headless N [--input SCRIPT]: the same frames, the same input, the same hash line. */
async function hashRun(n) {
  let script;
  try { script = new InputScript(query.get('input') ?? ''); } catch (e) { message(`input: ${e.message}`, 'error'); return; }
  const st = {};
  let done = 0, png = null;
  try {
    while (done < n) {
      const k = Math.min(BATCH, n - done);
      const steps = Array.from({ length: k }, (_, i) => {
        const f = done + i;
        return { pads: [script.pad(f), 0, 0, 0], text: script.textAt(f), input: script.raw(f, st, displaySize(), worker.inputMode) };
      });
      const r = await worker.frames(steps, done + k === n, { size: displaySize(), copy: done + k === n });
      png = r.png ?? png;
      done += k;
    }
  } catch (e) { if (!(e instanceof ProcExit)) { message(`hash run failed: ${e.message}`, 'error'); throw e; } }
  const s = worker.stats;
  const out = `frames=${s.frames} presented=${s.presented} size=${s.width}x${s.height} ` +
              `video_fnv32=${hex(s.videoHash)} audio_fnv32=${hex(s.audioHash)} audio_frames=${s.audioFrames}`;
  $('status').textContent = out;
  console.log(out);
  globalThis.__opennfspuPng = png;
  globalThis.__opennfspuResult = out;
}

$('stop').onclick = async () => { await stopGame(); endGameView(); };
addEventListener('pagehide', () => { worker?.exit(); });

// ---- choosing and importing the data --------------------------------------------------------
let checked = null; // result of data.folderFrom...

function showCheck(result) {
  checked = result;
  const list = $('check-list');
  list.textContent = '';
  const item = (text, ok) => {
    const li = document.createElement('li');
    li.className = ok ? 'ok' : 'bad';
    li.textContent = text;
    list.append(li);
  };
  const src = result.source;
  if (src.kind === 'image') item(`disc image "${src.name}": ${mbText(result.bytes)}`, true);
  else item(`folder "${src.name}": ${result.files} files, ${mbText(result.bytes)}`, true);
  if (result.problems.length) {
    for (const p of result.problems) item(p, false);
    item('This does not look like Porsche Unleashed: choose the folder the game is installed in, or an .iso of it.', false);
  } else if (src.kind === 'image') {
    item('ISO 9660 volume found: the game reads the image itself (it says so if the installed game isn\'t in it)', true);
  } else {
    item(`GameData, FEData and ${data.EXE} (${data.EXE_SIZE.toLocaleString('en')} bytes, version 3.5) found`, true);
  }
  show('check', true);
  show('check-actions', !result.problems.length);
  $('import').hidden = !hasOpfs;
  globalThis.__opennfspuChecked = { problems: result.problems, files: result.files, bytes: result.bytes, kind: src.kind };
}
const mbText = data.mb;

$('pick-folder').onclick = async () => {
  message('');
  if (!window.showDirectoryPicker) return $('folder-input').click(); // Firefox, Safari
  let handle;
  try { handle = await showDirectoryPicker({ id: 'opennfspu-data', mode: 'read' }); } catch (e) {
    if (e.name !== 'AbortError') message(e.message, 'error');
    return;
  }
  message('Reading the folder...');
  try { showCheck(await data.folderFromHandle(handle)); message(''); } catch (e) { message(`Could not read the folder: ${e.message}`, 'error'); }
};
$('pick-image').onclick = () => { message(''); $('image-input').click(); };
$('image-input').onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f) return;
  try { showCheck(await data.imageFromFile(f)); } catch (err) { message(`Could not read the image: ${err.message}`, 'error'); }
};
$('folder-input').onchange = (e) => {
  if (e.target.files.length) showCheck(data.folderFromFileList(e.target.files));
  e.target.value = '';
};

$('import').onclick = async () => {
  if (!checked) return;
  startAudio();
  const source = checked.source;
  show('check', false);
  show('progress', true);
  $('pick-folder').disabled = $('pick-image').disabled = true;
  const t0 = performance.now();
  try {
    await stopGame();
    const info = await data.importSource(source, (p) => {
      $('progress-bar').value = p.total ? p.bytes / p.total : 1;
      $('progress-text').textContent = `Copying ${p.file}: ${mbText(p.bytes)} of ${mbText(p.total)} (${p.done} of ${p.files} files)`;
    });
    $('progress-text').textContent = `Imported ${info.files} files, ${mbText(info.bytes)} in ${((performance.now() - t0) / 1000).toFixed(0)} s.`;
    globalThis.__opennfspuImported = info;
    await refreshImported();
    show('progress', false);
    await play('opfs');
  } catch (e) {
    show('progress', false);
    const full = /quota/i.test(`${e.name} ${e.message}`);
    message(full ? `Not enough browser storage for the game data (${mbText(checked.bytes)}). Free some space, or play without importing.`
                 : `Import failed: ${e.message}. You can still play without importing.`, 'error');
    show('check', true);
    await data.removeImport().catch(() => {});
    refreshImported();
  } finally {
    $('pick-folder').disabled = $('pick-image').disabled = false;
  }
};
$('play-direct').onclick = () => { if (checked) play(checked.source); };
$('play-opfs').onclick = () => play('opfs');
$('remove').onclick = async () => {
  await stopGame();
  show('game', false);
  try { await data.removeImport(); message('The imported game data was removed from this browser.'); } catch (e) { message(`Could not remove it: ${e.message}`, 'error'); }
  refreshImported();
};
$('persist').onclick = async () => {
  const ok = await data.requestPersist();
  message(ok ? 'The browser will keep the imported data.' : 'The browser did not agree to keep the data; it stays until space runs low.', ok ? 'info' : 'warn');
  refreshImported();
};

async function refreshImported() {
  const info = hasOpfs ? await data.existingImport() : null;
  show('imported', !!info);
  $('choose-title').textContent = info ? 'Your game' : 'Choose your game';
  if (info) {
    $('imported-desc').textContent = info.kind === 'image' ? `disc image "${info.name}", ${mbText(info.bytes)}`
      : `game folder "${info.name}", ${info.files} files, ${mbText(info.bytes)}`;
    const s = await data.storageInfo();
    $('storage').textContent = s.usage !== undefined
      ? `This site uses ${mbText(s.usage)} of the ${mbText(s.quota ?? 0)} the browser allows${s.persisted ? '; kept even when space runs low' : ''}.`
      : '';
    show('persist-row', !s.persisted && !!navigator.storage?.persist);
  }
  return info;
}

// ---- the saves: export / import (only from this page: it is hidden while a game runs) -------------
async function refreshSaves() {
  try {
    const d = saves.describe(await saves.listSaves());
    const n = (k, one, many) => `${k} ${k === 1 ? one : many}`;
    $('saves-desc').textContent = d.files
      ? `Kept in this browser: ${n(d.players, 'player', 'players')}, ${n(d.replays, 'replay', 'replays')} and ${n(d.files, 'file', 'files')} in all.`
      : 'Nothing saved in this browser yet.';
    $('export-saves').disabled = !d.files;
  } catch (e) {
    $('saves-desc').textContent = `The browser's storage can't be read: ${e.message}`;
    $('export-saves').disabled = $('import-saves').disabled = true;
  }
}
$('export-saves').onclick = async () => {
  try {
    const { blob, files } = await saves.exportSaves();
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `opennfspu-saves-${new Date().toISOString().slice(0, 10)}.zip`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    message(`Exported ${files} file${files === 1 ? '' : 's'} as ${a.download}.`);
  } catch (e) { message(`Could not export the saves: ${e.message}`, 'error'); }
};
$('import-saves').onclick = () => { message(''); $('saves-input').click(); };
$('saves-input').onchange = async () => {
  const picked = [...$('saves-input').files];
  $('saves-input').value = '';
  if (!picked.length) return;
  if (running) return message('Stop the game before importing saves.', 'warn');
  try {
    const r = await saves.importSaves(picked);
    const skip = r.skipped.length ? ` Skipped (not a save): ${r.skipped.join(', ')}.` : '';
    if (r.imported.length) message(`Imported ${r.imported.join(', ')}.${skip}`);
    else message(`Nothing to import.${skip}`, 'warn');
  } catch (e) { message(`Could not import the saves: ${e.message}`, 'error'); }
  refreshSaves();
};
refreshSaves();

// ---- the screen shape (a port addition: --param aspect=16:9 or 16:10), kept in this browser ---------
/** 'match my screen': the nearest of 4:3 (''), 16:10 and 16:9 to the screen's own shape. */
function screenShape() {
  const r = screen.width / screen.height;
  const shapes = [['', 4 / 3], ['16:10', 16 / 10], ['16:9', 16 / 9]];
  return shapes.reduce((a, b) => (Math.abs(r - b[1]) < Math.abs(r - a[1]) ? b : a))[0];
}
try {
  const v = localStorage.getItem(WIDE_KEY);   // ('1': 16:9, as the first switch kept it)
  $('aspect').value = v === '1' ? '16:9' : ['16:9', '16:10', 'auto'].includes(v) ? v : '';
} catch { /* no storage: 4:3 */ }
if (query.has('aspect')) {   // the address decides
  $('aspect').value = ['16:9', '16:10', 'auto'].includes(query.get('aspect')) ? query.get('aspect') : '';
  $('aspect').disabled = true;
}
$('aspect').onchange = () => {
  try { localStorage.setItem(WIDE_KEY, $('aspect').value); } catch { /* kept for this visit */ }
};

if (!hasOpfs) message('This browser has no private file storage (OPFS), so the game data can\'t be imported. Play without importing works.', 'warn');
if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') message('This browser can\'t run OpenNFSPU (it needs WebAssembly and Web Workers).', 'error');
else if (!hasGl || !hasOffscreen) message('This browser can\'t run OpenNFSPU: it needs WebGL 2 and OffscreenCanvas.', 'error');
const ready = refreshImported();
ready.then((info) => {
  globalThis.__opennfspuReady = true;
  if (info && query.has('autoplay')) play('opfs');
});
globalThis.opennfspuPlay = { play, stopGame, refreshImported, refreshSaves, framePng, data, saves };
