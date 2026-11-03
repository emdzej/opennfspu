// The game's saves in this browser (the players and their saves, the options in fe.txt, the HUD layouts,
// the track records, the replays): gasm keeps them in the IndexedDB database "gasm", store "kv", one record
// per key under "opennfspu/" (vendor/gasm/lib/storage.js). Exported as a zip of one file per key, the
// layout of the desktop runner's storage folder (<data dir>/gasm/opennfspu, or gasm-run --storage-dir), so
// the same files move both ways. The game reads the store once when it starts: import only while no game
// runs.

import { validKey, STORAGE_MAX_VALUE } from './vendor/gasm/lib/storage.js';

const NAMESPACE = 'opennfspu/';

function db() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open('gasm', 1);
    req.onupgradeneeded = () => req.result.createObjectStore('kv');
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

/** [[key, Uint8Array]] of the saved game data, sorted by key. */
export async function listSaves() {
  const d = await db();
  try {
    return await new Promise((resolve, reject) => {
      const out = [];
      const req = d.transaction('kv').objectStore('kv').openCursor(IDBKeyRange.bound(NAMESPACE, `${NAMESPACE}￿`));
      req.onsuccess = () => {
        const c = req.result;
        if (!c) return resolve(out.sort((a, b) => (a[0] < b[0] ? -1 : 1)));
        out.push([String(c.key).slice(NAMESPACE.length), new Uint8Array(c.value)]);
        c.continue();
      };
      req.onerror = () => reject(req.error);
    });
  } finally { d.close(); }
}

async function store(entries) {
  const d = await db();
  try {
    await new Promise((resolve, reject) => {
      const tx = d.transaction('kv', 'readwrite');
      for (const [k, v] of entries) tx.objectStore('kv').put(v, NAMESPACE + k);
      tx.oncomplete = resolve;
      tx.onerror = tx.onabort = () => reject(tx.error ?? new Error('aborted'));
    });
  } finally { d.close(); }
}

/** What the saves hold, for the page: the players (player.lst names the last one), replays, files. */
export function describe(files) {
  const names = files.map(([k]) => k);
  return {
    players: names.filter((k) => /^\d.*\.sav$/i.test(k)).length,
    replays: names.filter((k) => /\.rpl$/i.test(k)).length,
    files: names.length,
  };
}

// ---- zip (stored on export; stored or deflated on import, as the system's zip tools write it) --------
const CRC = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c;
});
const crc32 = (b) => { let c = ~0; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return ~c >>> 0; };

function dosTime(d) {
  return [(d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1),
    ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate()];
}

/** A zip Blob of [[name, Uint8Array]], stored. */
export function zip(files, date = new Date()) {
  const [time, day] = dosTime(date), enc = new TextEncoder(), parts = [], central = [];
  let offset = 0;
  for (const [name, data] of files) {
    const n = enc.encode(name), crc = crc32(data);
    const head = (sig, size) => {
      const h = new DataView(new ArrayBuffer(size));
      h.setUint32(0, sig, true);
      return h;
    };
    const local = head(0x04034b50, 30 + n.length);
    [[4, 20], [6, 0], [8, 0], [10, time], [12, day], [26, n.length], [28, 0]].forEach(([o, v]) => local.setUint16(o, v, true));
    [[14, crc], [18, data.length], [22, data.length]].forEach(([o, v]) => local.setUint32(o, v, true));
    new Uint8Array(local.buffer).set(n, 30);
    const dir = head(0x02014b50, 46 + n.length);
    [[4, 20], [6, 20], [8, 0], [10, 0], [12, time], [14, day], [28, n.length], [30, 0], [32, 0], [34, 0], [36, 0]]
      .forEach(([o, v]) => dir.setUint16(o, v, true));
    [[16, crc], [20, data.length], [24, data.length], [38, 0], [42, offset]].forEach(([o, v]) => dir.setUint32(o, v, true));
    new Uint8Array(dir.buffer).set(n, 46);
    parts.push(local.buffer, data);
    central.push(dir.buffer);
    offset += 30 + n.length + data.length;
  }
  const size = central.reduce((s, b) => s + b.byteLength, 0);
  const end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true);
  end.setUint16(8, files.length, true); end.setUint16(10, files.length, true);
  end.setUint32(12, size, true); end.setUint32(16, offset, true);
  return new Blob([...parts, ...central, end.buffer], { type: 'application/zip' });
}

async function inflate(data) {
  if (typeof DecompressionStream === 'undefined') throw new Error('this browser can\'t unpack compressed zips');
  const s = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(s).arrayBuffer());
}

/** [[name, Uint8Array]] of a zip's files (folders and paths dropped: the names only). */
export async function unzip(buf) {
  const b = new Uint8Array(buf), v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  let e = b.length - 22;
  while (e >= 0 && v.getUint32(e, true) !== 0x06054b50) e--;
  if (e < 0) throw new Error('not a zip file');
  const count = v.getUint16(e + 10, true), dec = new TextDecoder();
  let p = v.getUint32(e + 16, true);
  const out = [];
  for (let i = 0; i < count; i++) {
    if (v.getUint32(p, true) !== 0x02014b50) throw new Error('damaged zip file');
    const method = v.getUint16(p + 10, true), csize = v.getUint32(p + 20, true);
    const nlen = v.getUint16(p + 28, true), xlen = v.getUint16(p + 30, true), clen = v.getUint16(p + 32, true);
    const at = v.getUint32(p + 42, true);
    const path = dec.decode(b.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + xlen + clen;
    if (path.endsWith('/') || path.startsWith('__MACOSX/')) continue;
    const start = at + 30 + v.getUint16(at + 26, true) + v.getUint16(at + 28, true);
    const raw = b.subarray(start, start + csize);
    let data;
    if (method === 0) data = raw.slice();
    else if (method === 8) data = await inflate(raw);
    else throw new Error(`${path}: packed in a way this page can't read (method ${method})`);
    out.push([path.split('/').pop(), data]);
  }
  return out;
}

/** The saves as a zip Blob, and how many files are in it. */
export async function exportSaves() {
  const files = await listSaves();
  return { blob: zip(files), files: files.length };
}

/** Import a zip of saves, or the save files themselves; same names are replaced, the rest kept.
 *  Returns { imported: [names], skipped: [names] }. */
export async function importSaves(fileList) {
  const files = [];
  for (const f of fileList) {
    const buf = new Uint8Array(await f.arrayBuffer());
    if (/\.zip$/i.test(f.name) || (buf[0] === 0x50 && buf[1] === 0x4b && buf[2] === 3 && buf[3] === 4)) files.push(...await unzip(buf));
    else files.push([f.name, buf]);
  }
  const ok = files.filter(([k, v]) => validKey(k) && !k.startsWith('.') && v.length <= STORAGE_MAX_VALUE);
  const skipped = files.filter((f) => !ok.includes(f)).map(([k]) => k);
  if (ok.length) await store(ok);
  return { imported: ok.map(([k]) => k), skipped };
}
