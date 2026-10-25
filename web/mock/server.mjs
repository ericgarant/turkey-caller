// Mock of the ESP32 API so the page can be built and tested without the board.
// No dependencies: node web/mock/server.mjs [port]   (default 8080)
// Behaves like the firmware contract in docs/architecture.md: form-encoded POSTs,
// JSON replies, {ok:false,error} on failure, status pushes on WS /ws.
import http from "node:http";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const PAGE = path.join(here, "..", "index.html");
const PORT = +(process.argv[2] || process.env.PORT || 8080);
const FS_TOTAL = 0x160000; // 1.375 MB LittleFS
const BYTES_PER_MS = 8; // 64 kbps

const sounds = [
  ["tree_call", "Tree call", "soft", 6200], ["cluck", "Cluck", "soft", 1800], ["purr", "Purr", "soft", 3400],
  ["cluck_purr", "Cluck & purr", "soft", 4800], ["whine", "Whine", "soft", 5000],
  ["plain_yelp", "Plain yelp", "loud", 3600], ["assembly_yelp", "Assembly yelp", "loud", 8400],
  ["cutting", "Cutting", "loud", 4200], ["fly_down_cackle", "Fly-down cackle", "loud", 3000],
  ["gobble", "Gobble", "care", 2600], ["putt", "Putt", "care", 1200],
  ["dindon_purr", "Purr & clucks", "pack", 7500], ["dindon_yelp", "Yelp", "pack", 3200],
  ["dindon_fast_yelp", "Fast yelp", "pack", 3100], ["dindon_kee_kee", "Kee-kee run", "pack", 3800],
  ["dindon_gobble", "Gobble", "pack", 2900],
].map(([id, name, group, ms]) => ({ id, name, animal: "turkey", group, ms, bytes: ms * BYTES_PER_MS }));

// Call packs live on the SD card (F14). Off at start, like a board with no card; the test hook
// POST /api/mock/pack name=moose on=1|0 inserts or pulls one. Deer lengths are the real clips.
const PACKS = {
  deer: [["deer_doe_estrus", "Doe in estrus", "Doe bleat in heat, draws a buck in", 23900], ["deer_doe_bleat", "Doe bleat", "Plaintive doe call, calm, all day", 19900],
    ["deer_tending_grunt", "Tending grunt", "Buck grunting while he tends a doe", 6400], ["deer_rattle", "Antler rattle", "Rattling antlers, mimics two bucks sparring", 13700],
    ["deer_buck_grunt", "Buck grunt", "Rut grunting, long series", 17400], ["deer_trail_grunt", "Trailing grunt", "Buck following a doe's trail", 3900]]
    .map(([id, name, hint, ms]) => ({ id, name, animal: "deer", group: "deer", hint, ms, bytes: ms * BYTES_PER_MS })),
  // Placeholder moose clips for the tab tests only; the real moose pack comes from Eric's moose folders.
  moose: [["moose_cow_call", "Cow call", "Placeholder", 9000], ["moose_bull_grunt", "Bull grunt", "Placeholder", 12000]]
    .map(([id, name, hint, ms]) => ({ id, name, animal: "moose", group: "moose", hint, ms, bytes: ms * BYTES_PER_MS })),
};

const NEARBY = [
  { name: "Level Box Slim", addr: "5C:49:7D:1A:20:8E", rssi: -52 },
  { name: "Cooboom 20W", addr: "41:42:1F:6B:03:C2", rssi: -67 },
  { name: "JBL Flip 5", addr: "F8:DF:15:90:4A:11", rssi: -84 },
  { name: "", addr: "00:1A:7D:DA:71:13", rssi: -88 }, // nameless: Phase 0 cannot pick it
];

const DEFAULTS = { ap_ssid: "TurkeyCaller", ap_pass: "gobble123", device_name: "Turkey Caller", led_motion: 1 };
const cfg = { ...DEFAULTS };
// Like the board: a random non-zero rev at each boot, +1 on every saved change (F05)
const newRev = () => 1 + crypto.randomInt(0x7ffffffe);
let rev = newRev(), downUntil = 0;
const st = { bt: "connected", speaker: "Level Box Slim", stream: "ready", error: "", playing: false, current: "", loop: false, repeat: false, volume: 70 };
// Test hook (mock only): POST /api/mock/speaker mode=asleep|off|on. Asleep: connected, stream
// suspended, a tap wakes it in 1 s. Off: saved but out of reach, a tap gives up after 8 s.
let speakerMode = "on", wakeTimer = null;
let playStart = 0, playTimer = null, scanning = false, devices = [];
let busy = 0, busyUntil = 0;  // test hook (mock only): POST /api/mock/busy count=N (next N API calls) or ms=T (every API call for T ms) answers 503, like the board's busy window
const boot = Date.now();
// Motion sensor (F13): POST /api/mock/motion on=1|0 drives the PIR.
let motionNow = false, motionAt = 0;

// SD card (F12): present when a pack is inserted or the card is set on; POST /api/mock/sd on=0|1 (empty = flash only again).
// Off by default so the older tests see a board with flash only (no sd fields); on=1 gives a 1 GB card, on=0 no card.
let sdOn = null;
const SD_TOTAL = 960 * 1024 * 1024;
function sdStatus() {
  if (sdOn === null) return {};
  return sdOn ? { sdTotal: SD_TOTAL, sdFree: SD_TOTAL - 5 * 1024 * 1024 } : { sdTotal: 0, sdFree: 0 };
}

function status() {
  const s = sounds.find((x) => x.id === st.current);
  const len = s ? s.ms : 0;
  let pos = st.playing ? Date.now() - playStart : 0;
  if (st.loop && len) pos %= len;
  const used = sounds.reduce((a, x) => a + x.bytes, 0) + 18000;
  return {
    ...st, cfg: rev, pos_ms: pos, len_ms: len, plays: 0, underruns: 0,
    heap: 70000 + Math.round(Math.random() * 3000), minHeap: 52880, maxBlock: 40000,
    fsFree: FS_TOTAL - used, fsTotal: FS_TOTAL, ...sdStatus(), motion: !!motionNow, motionAt, clients: sockets.size || 1,
    uptime: Math.floor((Date.now() - boot) / 1000) + 4380, version: "mock",
  };
}

// ---- WebSocket (just enough of RFC 6455 for small text frames) ----
const sockets = new Set();
function wsSend(sock, text) {
  const data = Buffer.from(text);
  const head = data.length < 126 ? Buffer.from([0x81, data.length])
    : Buffer.from([0x81, 126, data.length >> 8, data.length & 255]);
  sock.write(Buffer.concat([head, data]));
}
function push() { const j = JSON.stringify(status()); for (const s of sockets) wsSend(s, j); }
setInterval(() => { if (st.playing) push(); }, 2000);
setInterval(push, 10000);

function wsFrames(sock, buf) {
  while (buf.length >= 2) {
    const op = buf[0] & 15; let len = buf[1] & 127, off = 2;
    if (len === 126) { len = buf.readUInt16BE(2); off = 4; } else if (len === 127) return sock.destroy();
    const mask = buf.subarray(off, off + 4); off += 4;
    if (buf.length < off + len) return buf;
    const data = Buffer.from(buf.subarray(off, off + len)).map((b, i) => b ^ mask[i % 4]);
    buf = buf.subarray(off + len);
    if (op === 8) { sockets.delete(sock); sock.end(); return; }
    if (op === 1) {
      try {
        const m = JSON.parse(data.toString());
        if (m.cmd === "play") play(m.id, !!m.loop);
        if (m.cmd === "stop") stop();
        if (m.cmd === "volume") { st.volume = clamp(+m.level); push(); }
        if (m.cmd === "repeat") setRepeat(!!m.on && m.on !== "0");
      } catch { /* ignore bad frames */ }
    }
  }
  return buf;
}

// ---- Behaviour ----
const clamp = (v) => Math.max(0, Math.min(100, Math.round(v) || 0));
function play(id, loop) {
  const s = sounds.find((x) => x.id === id);
  if (!s) return "bad sound id";
  if (!st.speaker) return "No speaker connected";
  st.error = "";
  if (st.bt !== "connected" || st.stream !== "ready") {  // like the board (F07): answer 202, wake, then play
    stop(false);
    clearTimeout(wakeTimer);
    st.stream = "waking"; push();
    if (speakerMode === "off") wakeTimer = setTimeout(() => { Object.assign(st, { stream: "down", error: "Speaker did not answer" }); push(); }, 8000);
    else wakeTimer = setTimeout(() => { Object.assign(st, { bt: "connected", stream: "ready" }); speakerMode = "on"; play(id, loop); }, 1000);
    return "waking";
  }
  clearTimeout(playTimer);
  Object.assign(st, { playing: true, current: id, loop, repeat: loop });
  playStart = Date.now();
  if (!loop) playTimer = setTimeout(stop, s.ms);
  push();
}
function setRepeat(on) {
  st.repeat = on;
  if (st.playing && st.loop !== on) {  // like the board: the call playing now follows the switch
    st.loop = on;
    clearTimeout(playTimer);
    if (!on) { const s = sounds.find((x) => x.id === st.current); playTimer = setTimeout(stop, s.ms - ((Date.now() - playStart) % s.ms)); }
  }
  push();
}
function stop(cancelWake = true) {
  clearTimeout(playTimer);
  if (cancelWake && st.stream === "waking") { clearTimeout(wakeTimer); st.stream = st.bt === "connected" ? "suspended" : "down"; }
  st.playing = false; st.current = ""; push();
}

// ---- HTTP ----
const json = (res, code, obj) => { res.writeHead(code, { "Content-Type": "application/json" }); res.end(JSON.stringify(obj)); };
const ok = (res) => json(res, 200, { ok: true });
const fail = (res, code, error) => json(res, code, { ok: false, error });

function body(req) {
  return new Promise((resolve) => { const parts = []; req.on("data", (c) => parts.push(c)); req.on("end", () => resolve(Buffer.concat(parts))); });
}

// A restart: tell every page, drop all connections, refuse new ones for 3 s, come back with a new rev.
function restart(reason) {
  for (const s of sockets) wsSend(s, JSON.stringify({ evt: "restart", reason, ssid: cfg.ap_ssid }));
  setTimeout(() => {
    stop(); downUntil = Date.now() + 3000; rev = newRev();
    for (const s of sockets) s.destroy();
    sockets.clear();
  }, 1000);
}

const server = http.createServer(async (req, res) => {
  if (Date.now() < downUntil) return req.socket.destroy();
  const url = new URL(req.url, "http://x");
  const raw = await body(req);
  const isForm = (req.headers["content-type"] || "").startsWith("application/x-www-form-urlencoded");
  const p = Object.fromEntries(new URLSearchParams(isForm ? raw.toString() : url.search));
  const route = req.method + " " + url.pathname;
  if (process.env.MOCK_LOG) console.log(route, JSON.stringify(p));

  if (route === "POST /api/mock/sd") { sdOn = p.on === "" ? null : p.on === "1"; if (sdOn === false) st.error = "SD card removed"; push(); return ok(res); }
  if (route === "POST /api/mock/pack") {
    const pack = PACKS[p.name]; if (!pack) return fail(res, 404, "no such pack");
    for (const c of pack) { const i = sounds.findIndex((x) => x.id === c.id); if (i >= 0) sounds.splice(i, 1); }
    if (p.on === "1") sounds.push(...pack.map((c) => ({ ...c })));
    return ok(res);
  }
  if (route === "POST /api/mock/motion") { motionNow = p.on === "1"; if (motionNow) motionAt = Math.floor((Date.now() - boot) / 1000) + 4380; push(); return ok(res); }
  if (route === "POST /api/mock/busy") { busy = +p.count || 0; busyUntil = Date.now() + (+p.ms || 0); return ok(res); }
  if (url.pathname.startsWith("/api/") && (busy > 0 || Date.now() < busyUntil)) { if (busy > 0) busy--; return fail(res, 503, "busy, try again"); }

  switch (route) {
    case "GET /": case "GET /index.html":
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      return res.end(fs.readFileSync(PAGE));
    case "GET /api/status": return json(res, 200, status());
    case "GET /api/sounds": return json(res, 200, sounds);
    case "POST /api/play": {
      const e = play(p.id, p.loop === "1" || p.loop === "true");
      if (e === "waking") return json(res, 202, { ok: true, waking: true });
      return e ? fail(res, /speaker/i.test(e) ? 409 : 400, e) : ok(res);
    }
    case "POST /api/mock/speaker":
      speakerMode = p.mode || "on";
      if (speakerMode === "asleep") Object.assign(st, { bt: "connected", stream: "suspended" });
      else if (speakerMode === "off") { stop(); Object.assign(st, { bt: "idle", stream: "down" }); }
      else Object.assign(st, { bt: "connected", stream: "ready" });
      push(); return ok(res);
    case "POST /api/stop": stop(); return ok(res);
    case "POST /api/repeat": if (p.on === undefined) return fail(res, 400, "missing on"); setRepeat(p.on === "1" || p.on === "true"); return ok(res);
    case "POST /api/volume": if (p.level === undefined) return fail(res, 400, "missing level"); st.volume = clamp(+p.level); push(); return ok(res);
    case "POST /api/bt/scan":
      stop(); scanning = true; devices = [];
      NEARBY.forEach((d, i) => setTimeout(() => devices.push(d), 2000 + i * 2000));
      setTimeout(() => { scanning = false; }, 10000);
      return ok(res);
    case "GET /api/bt/devices": return json(res, 200, { scanning, devices });
    case "POST /api/bt/connect": {
      if (!p.name && !p.addr) return fail(res, 400, "missing addr");
      if (p.addr && !/^[0-9a-f]{2}(:[0-9a-f]{2}){5}$/i.test(p.addr)) return fail(res, 400, "addr must look like aa:bb:cc:dd:ee:ff");
      Object.assign(st, { bt: "connecting", stream: "down", error: "", speaker: p.name || p.addr }); push();
      setTimeout(() => { Object.assign(st, { bt: "connected", stream: "ready" }); push(); }, 2500);
      return ok(res);
    }
    case "POST /api/bt/forget": stop(); Object.assign(st, { bt: "idle", stream: "down", error: "", speaker: "" }); push(); return ok(res);
    case "GET /api/config":
      return json(res, 200, { ap_ssid: cfg.ap_ssid, device_name: cfg.device_name, led_motion: cfg.led_motion, default_pass: cfg.ap_pass === DEFAULTS.ap_pass, rev });
    case "POST /api/config": {
      if (p.rev !== undefined && +p.rev !== rev) return fail(res, 409, "changed on another phone");
      const ssid = p.ap_ssid ?? cfg.ap_ssid, name = p.device_name ?? cfg.device_name, pass = p.ap_pass || "";
      const bytes = (v) => Buffer.byteLength(v);  // like the board: limits in UTF-8 bytes
      if (!ssid || bytes(ssid) > 32) return fail(res, 400, "Wi-Fi name must be 1 to 32 characters");
      if (!name || bytes(name) > 24) return fail(res, 400, "device name must be 1 to 24 characters");
      if (pass && /[^\x20-\x7e]/.test(pass)) return fail(res, 400, "password must be printable ASCII");
      if (pass && (bytes(pass) < 8 || bytes(pass) > 63)) return fail(res, 400, "password must be 8 to 63 characters");
      if (p.led_motion !== undefined && p.led_motion !== "0" && p.led_motion !== "1") return fail(res, 400, "led_motion must be 0 or 1");
      if (p.led_motion !== undefined) cfg.led_motion = +p.led_motion;  // F13
      const wifi = ssid !== cfg.ap_ssid || (pass && pass !== cfg.ap_pass);
      Object.assign(cfg, { ap_ssid: ssid, device_name: name }, pass ? { ap_pass: pass } : {});
      rev++;
      json(res, 200, { ok: true, restart: !!wifi });
      if (wifi) restart("settings"); else push();
      return;
    }
    // Test hook: holding BOOT for 5 s on the board
    case "POST /api/mock/boot-reset": Object.assign(cfg, DEFAULTS); rev++; ok(res); return restart("reset");
    case "POST /api/sounds": {
      if (st.playing) return fail(res, 409, "stop playback first");
      const m = /filename="([a-z0-9_]{1,24})\.wav"/.exec(raw.toString("latin1"));
      if (!m) return fail(res, 400, "file must be name.wav with a-z, 0-9 and _");
      if (raw.length > status().fsFree) return fail(res, 413, "not enough room");
      const ms = Math.round(raw.length / BYTES_PER_MS);
      const old = sounds.find((x) => x.id === m[1]);
      if (old) Object.assign(old, { bytes: raw.length, ms });
      else sounds.push({ id: m[1], name: m[1], animal: "turkey", group: "other", ms, bytes: raw.length });
      return ok(res);
    }
    case "POST /api/sounds/delete": {
      if (st.playing) return fail(res, 409, "stop playback first");
      const i = sounds.findIndex((x) => x.id === p.id);
      if (i < 0) return fail(res, 404, "no such sound");
      sounds.splice(i, 1); return ok(res);
    }
  }
  if (url.pathname.startsWith("/api/")) return fail(res, 404, "not found");
  res.writeHead(302, { Location: "/" }); res.end();
});

server.on("upgrade", (req, sock) => {
  if (req.url !== "/ws" || Date.now() < downUntil) return sock.destroy();
  const accept = crypto.createHash("sha1").update(req.headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
  sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  sockets.add(sock);
  let pending = Buffer.alloc(0);
  sock.on("data", (d) => { pending = wsFrames(sock, Buffer.concat([pending, d])) || Buffer.alloc(0); });
  sock.on("close", () => sockets.delete(sock));
  sock.on("error", () => sockets.delete(sock));
  wsSend(sock, JSON.stringify(status()));
});

server.listen(PORT, () => console.log(`Mock Turkey Caller on http://localhost:${PORT}`));
