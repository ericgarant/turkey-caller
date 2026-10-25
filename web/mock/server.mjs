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
  ["kee_kee_run", "Kee-kee run", "loud", 5200], ["gobble", "Gobble", "care", 2600], ["putt", "Putt", "care", 1200],
].map(([id, name, group, ms]) => ({ id, name, group, ms, bytes: ms * BYTES_PER_MS }));

const NEARBY = [
  { name: "Level Box Slim", addr: "5C:49:7D:1A:20:8E", rssi: -52 },
  { name: "Cooboom 20W", addr: "41:42:1F:6B:03:C2", rssi: -67 },
  { name: "JBL Flip 5", addr: "F8:DF:15:90:4A:11", rssi: -84 },
  { name: "", addr: "00:1A:7D:DA:71:13", rssi: -88 }, // nameless: Phase 0 cannot pick it
];

const cfg = { ap_ssid: "TurkeyCaller", ap_pass: "gobble123", device_name: "Turkey Caller" };
const st = { bt: "connected", speaker: "Level Box Slim", playing: false, current: "", loop: false, repeat: false, volume: 70 };
let playStart = 0, playTimer = null, scanning = false, devices = [];
let busy = 0, busyUntil = 0;  // test hook (mock only): POST /api/mock/busy count=N (next N API calls) or ms=T (every API call for T ms) answers 503, like the board's busy window
const boot = Date.now();

function status() {
  const s = sounds.find((x) => x.id === st.current);
  const len = s ? s.ms : 0;
  let pos = st.playing ? Date.now() - playStart : 0;
  if (st.loop && len) pos %= len;
  const used = sounds.reduce((a, x) => a + x.bytes, 0) + 18000;
  return {
    ...st, pos_ms: pos, len_ms: len, plays: 0, underruns: 0,
    heap: 70000 + Math.round(Math.random() * 3000), minHeap: 52880, maxBlock: 40000,
    fsFree: FS_TOTAL - used, fsTotal: FS_TOTAL, clients: sockets.size || 1,
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
  if (st.bt !== "connected") return "speaker not connected";
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
function stop() { clearTimeout(playTimer); st.playing = false; st.current = ""; push(); }

// ---- HTTP ----
const json = (res, code, obj) => { res.writeHead(code, { "Content-Type": "application/json" }); res.end(JSON.stringify(obj)); };
const ok = (res) => json(res, 200, { ok: true });
const fail = (res, code, error) => json(res, code, { ok: false, error });

function body(req) {
  return new Promise((resolve) => { const parts = []; req.on("data", (c) => parts.push(c)); req.on("end", () => resolve(Buffer.concat(parts))); });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://x");
  const raw = await body(req);
  const isForm = (req.headers["content-type"] || "").startsWith("application/x-www-form-urlencoded");
  const p = Object.fromEntries(new URLSearchParams(isForm ? raw.toString() : url.search));
  const route = req.method + " " + url.pathname;
  if (process.env.MOCK_LOG) console.log(route, JSON.stringify(p));

  if (route === "POST /api/mock/busy") { busy = +p.count || 0; busyUntil = Date.now() + (+p.ms || 0); return ok(res); }
  if (url.pathname.startsWith("/api/") && (busy > 0 || Date.now() < busyUntil)) { if (busy > 0) busy--; return fail(res, 503, "busy, try again"); }

  switch (route) {
    case "GET /": case "GET /index.html":
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      return res.end(fs.readFileSync(PAGE));
    case "GET /api/status": return json(res, 200, status());
    case "GET /api/sounds": return json(res, 200, sounds);
    case "POST /api/play": { const e = play(p.id, p.loop === "1" || p.loop === "true"); return e ? fail(res, e.startsWith("speaker") ? 409 : 400, e) : ok(res); }
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
      if (!p.name) return fail(res, 400, "missing name");
      Object.assign(st, { bt: "connecting", speaker: p.name }); push();
      setTimeout(() => { st.bt = "connected"; push(); }, 2500);
      return ok(res);
    }
    case "POST /api/bt/forget": stop(); Object.assign(st, { bt: "idle", speaker: "" }); push(); return ok(res);
    case "GET /api/config": return json(res, 200, { ap_ssid: cfg.ap_ssid, device_name: cfg.device_name });
    case "POST /api/config":
      if (!p.ap_ssid || p.ap_ssid.length > 32) return fail(res, 400, "Wi-Fi name must be 1 to 32 characters");
      if (p.ap_pass && (p.ap_pass.length < 8 || p.ap_pass.length > 63)) return fail(res, 400, "password must be 8 to 63 characters");
      Object.assign(cfg, { ap_ssid: p.ap_ssid, device_name: p.device_name || cfg.device_name }, p.ap_pass ? { ap_pass: p.ap_pass } : {});
      return ok(res);
    case "POST /api/sounds": {
      if (st.playing) return fail(res, 409, "stop playback first");
      const m = /filename="([a-z0-9_]{1,24})\.wav"/.exec(raw.toString("latin1"));
      if (!m) return fail(res, 400, "file must be name.wav with a-z, 0-9 and _");
      if (raw.length > status().fsFree) return fail(res, 413, "not enough room");
      const ms = Math.round(raw.length / BYTES_PER_MS);
      const old = sounds.find((x) => x.id === m[1]);
      if (old) Object.assign(old, { bytes: raw.length, ms });
      else sounds.push({ id: m[1], name: m[1], group: "other", ms, bytes: raw.length });
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
  if (req.url !== "/ws") return sock.destroy();
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
