// Feature tests against the mock board: volume, Repeat shared by two clients, busy (503) recovery,
// speaker wake and reconnect (F07), and the parked upload tests. Talks HTTP and WebSocket only, no browser.
//   node test/features.mjs                     starts the mock (npm run test:features)
//   node test/features.mjs http://localhost:N  uses a mock already running
// Sections that need a mock hook the running mock does not have are skipped and say so.
// Parked tests (phone upload, deferred in PR #12) only run with RUN_PARKED=1.
import { spawn } from "node:child_process";
import assert from "node:assert/strict";

let base = process.argv[2], server;
if (!base) {
  server = spawn(process.execPath, ["mock/server.mjs", "8095"], { stdio: ["ignore", "pipe", "inherit"] });
  await new Promise((r) => server.stdout.once("data", r));
  base = "http://localhost:8095";
}
// This test forgets and re-pairs the speaker with a fake address, so it must not run against the real board.
if (!/^https?:\/\/(localhost|127\.0\.0\.1)(:\d+)?\/?$/.test(base) && !process.env.DESTRUCTIVE) {
  console.error("features.mjs is for the mock only: it forgets the speaker. Set DESTRUCTIVE=1 to run it against " + base);
  process.exit(1);
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const get = async (p) => (await fetch(base + p)).json();
const post = async (p, form = {}) => {
  const r = await fetch(base + p, { method: "POST", body: new URLSearchParams(form) });
  return { code: r.status, body: await r.json().catch(() => ({})) };
};
const status = () => get("/api/status");
const until = async (fn, ms = 3000, what = "condition") => {
  for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(40)) { const v = await fn(); if (v) return v; }
  throw new Error("timed out waiting for " + what);
};

// A client like a phone's page: keeps the last status pushed on /ws.
const sockets = [];
async function phone() {
  const ws = new WebSocket(base.replace("http", "ws") + "/ws");
  const c = { ws, last: null, pushes: 0, send: (m) => ws.send(JSON.stringify(m)) };
  ws.onmessage = (e) => { c.last = JSON.parse(e.data); c.pushes++; };
  await new Promise((ok, no) => { ws.onopen = ok; ws.onerror = () => no(new Error("websocket failed")); });
  await until(() => c.last, 2000, "first status push");
  sockets.push(ws);
  return c;
}

const failures = [];
const step = async (name, fn) => {
  try { await fn(); console.log("ok -", name); } catch (e) { failures.push(name); console.error("FAIL -", name + ":", e.message); }
};
const parked = async (name, why, fn) => {
  if (process.env.RUN_PARKED !== "1") return console.log("parked -", name, `(${why}; RUN_PARKED=1 runs it)`);
  await step("[parked] " + name, fn);
};
// A mock hook answers {ok:true}. A board (captive portal) answers unknown URLs with the page, so only that counts.
const hasHook = async (path, form) => { const r = await post(path, form); return r.code === 200 && r.body.ok === true; };

try {
  await post("/api/stop");
  const original = await status();

  // ---- Control page data and volume (F01, F08) ----
  await step("a phone joining gets the status at once, with every field the page reads", async () => {
    const a = await phone();
    for (const k of ["bt", "speaker", "playing", "current", "loop", "repeat", "volume", "heap", "minHeap", "underruns"]) assert.ok(k in a.last, "missing " + k);
  });
  await step("sound list: 12 calls in order, three groups, unique ids", async () => {
    const list = await get("/api/sounds");
    assert.equal(list.length, 12);
    assert.equal(new Set(list.map((s) => s.id)).size, 12);
    assert.deepEqual([...new Set(list.map((s) => s.group))], ["soft", "loud", "care"]);
  });
  await step("every one of the 12 calls plays and reports its length", async () => {
    for (const s of await get("/api/sounds")) {
      assert.equal((await post("/api/play", { id: s.id, loop: "0" })).code, 200, s.id);
      const st = await status();
      assert.deepEqual([st.playing, st.current, st.len_ms], [true, s.id, s.ms], s.id);
    }
    await post("/api/stop"); // ending by itself is covered in api.mjs
  });
  await step("volume: HTTP, WebSocket and clamping all end in one level, and every phone sees it within 1 s", async () => {
    const a = await phone(), b = await phone();
    await post("/api/volume", { level: "30" });
    await until(() => a.last.volume === 30 && b.last.volume === 30, 1000, "both phones at 30");
    a.send({ cmd: "volume", level: 45 });
    await until(() => b.last.volume === 45, 1000, "b follows a's WebSocket volume");
    assert.equal((await status()).volume, 45);
    a.send({ cmd: "volume", level: 500 });
    await until(() => b.last.volume === 100, 1000, "clamp high");
    a.send({ cmd: "volume", level: -5 });
    await until(() => b.last.volume === 0, 1000, "clamp low");
    await post("/api/volume", { level: String(original.volume) });
  });
  await step("volume changes while a call plays do not stop it", async () => {
    await post("/api/play", { id: "assembly_yelp", loop: "0" });
    await post("/api/volume", { level: "20" });
    await post("/api/volume", { level: String(original.volume) });
    const s = await status();
    assert.deepEqual([s.playing, s.current], [true, "assembly_yelp"]);
    await post("/api/stop");
  });
  await step("garbage WebSocket frames are ignored and the socket stays up", async () => {
    const a = await phone();
    a.ws.send("not json"); a.ws.send(JSON.stringify({ cmd: "nonsense" }));
    await post("/api/volume", { level: "41" });
    await until(() => a.last.volume === 41, 1000, "still receiving pushes");
    await post("/api/volume", { level: String(original.volume) });
  });

  // ---- Repeat shared by every phone (F04) ----
  await step("Repeat: one switch for every phone; both phones are told at once", async () => {
    const a = await phone(), b = await phone();
    await post("/api/repeat", { on: "1" });
    await until(() => a.last.repeat === true && b.last.repeat === true, 1000, "both see Repeat on");
    b.send({ cmd: "repeat", on: false });
    await until(() => a.last.repeat === false && b.last.repeat === false, 1000, "a follows b's WebSocket switch");
  });
  await step("Repeat: a phone that opens later (or reloads) sees the switch and the loop playing", async () => {
    await post("/api/repeat", { on: "1" });
    await post("/api/play", { id: "putt", loop: "1" });
    const late = await phone();
    assert.deepEqual([late.last.repeat, late.last.loop, late.last.playing, late.last.current], [true, true, true, "putt"]);
    await sleep(1500); // putt is 1.2 s: a loop is still going
    const s = await status();
    assert.equal(s.playing, true);
    assert.ok(s.pos_ms < s.len_ms, "pos_ms wraps at each pass");
    await post("/api/stop");
    await post("/api/repeat", { on: "0" });
  });
  await step("Repeat: switching it while a call plays changes that call; off lets the pass finish, on loops it", async () => {
    await post("/api/repeat", { on: "0" });
    await post("/api/play", { id: "cutting", loop: "0" }); // 4.2 s
    await post("/api/repeat", { on: "1" });
    let s = await status();
    assert.deepEqual([s.loop, s.repeat, s.playing], [true, true, true]);
    await post("/api/repeat", { on: "0" });
    s = await status();
    assert.deepEqual([s.loop, s.repeat, s.playing], [false, false, true], "finishes its pass, then stops");
    await post("/api/stop");
  });
  await step("Repeat: tapping another call replaces the loop and keeps the setting; Stop ends it for everyone", async () => {
    const a = await phone(), b = await phone();
    await post("/api/play", { id: "cluck", loop: "1" });
    await post("/api/play", { id: "purr", loop: "1" });
    await until(() => b.last.current === "purr" && b.last.loop, 1000, "b sees the replacement looping");
    assert.equal(b.last.repeat, true);
    await post("/api/stop");
    await until(() => !a.last.playing && !b.last.playing, 1000, "both stopped");
    await post("/api/repeat", { on: "0" });
  });
  await step("Repeat: a loop outlasts a full pass of the call", async () => {
    await post("/api/play", { id: "putt", loop: "1" }); // 1.2 s
    await sleep(1800);
    assert.equal((await status()).playing, true);
    await post("/api/stop");
    await post("/api/repeat", { on: "0" });
  });
  await step("Repeat: forgetting the speaker ends a loop", async () => {
    await post("/api/play", { id: "cluck", loop: "1" });
    await post("/api/bt/forget");
    assert.equal((await status()).playing, false);
    await post("/api/bt/connect", { addr: "5C:49:7D:1A:20:8E", name: "Level Box Slim" });
    await until(async () => (await status()).bt === "connected", 4000, "reconnected");
    assert.equal((await status()).playing, false, "nothing plays by itself after reconnecting");
  });

  // ---- Busy and request-limit recovery (F10) ----
  if (!(await hasHook("/api/mock/busy", { count: "0" }))) console.log("skip - busy recovery (this mock has no /api/mock/busy)");
  else {
    await step("busy: the board answers 503 for a while, then serves again by itself", async () => {
      await post("/api/mock/busy", { count: "3" });
      const codes = [];
      for (let i = 0; i < 4; i++) codes.push((await fetch(base + "/api/status")).status);
      assert.deepEqual(codes, [503, 503, 503, 200]);
    });
    await step("busy: a 503 is a JSON error the page can retry, and changes nothing", async () => {
      await post("/api/volume", { level: "55" });
      await post("/api/mock/busy", { count: "1" });
      const r = await post("/api/volume", { level: "10" });
      assert.equal(r.code, 503);
      assert.equal(r.body.ok, false);
      assert.equal((await status()).volume, 55);
      await post("/api/volume", { level: String(original.volume) });
    });
    await step("busy: WebSocket phones keep their connection and the page data through a busy window", async () => {
      const a = await phone();
      await post("/api/mock/busy", { count: "2" });
      await fetch(base + "/api/sounds");
      await fetch(base + "/api/sounds");
      await post("/api/volume", { level: "33" });
      await until(() => a.last.volume === 33, 1000, "push still arrives");
      await post("/api/volume", { level: String(original.volume) });
    });
  }

  // ---- Speaker reconnect and wake (F06, F07; PR #14) ----
  if (!(await hasHook("/api/mock/speaker", { mode: "on" }))) {
    console.log("skip - speaker wake and reconnect (this mock has no /api/mock/speaker; arrives with PR #14)");
  } else {
    await step("wake: a tap on a sleeping speaker answers 202 at once, wakes it, then plays; phones see waking then ready", async () => {
      const a = await phone();
      await post("/api/mock/speaker", { mode: "asleep" });
      assert.equal((await status()).stream, "suspended");
      const t0 = Date.now();
      const r = await post("/api/play", { id: "cluck", loop: "0" });
      assert.deepEqual([r.code, r.body.waking], [202, true]);
      assert.ok(Date.now() - t0 < 500, "/api/play never waits");
      await until(() => a.last.stream === "waking", 1000, "waking pushed");
      assert.equal(a.last.playing, false, "nothing plays while waking");
      await until(() => a.last.stream === "ready" && a.last.playing && a.last.current === "cluck", 4000, "ready and playing");
      await post("/api/stop");
    });
    await step("wake: a second tap while waking replaces the first call", async () => {
      await post("/api/mock/speaker", { mode: "asleep" });
      assert.equal((await post("/api/play", { id: "cluck" })).code, 202);
      assert.equal((await post("/api/play", { id: "purr" })).code, 202);
      const s = await until(async () => { const x = await status(); return x.playing && x; }, 4000, "playing");
      assert.equal(s.current, "purr");
      await post("/api/stop");
    });
    await step("wake: Stop while waking cancels, and nothing stale plays afterwards", async () => {
      await post("/api/mock/speaker", { mode: "asleep" });
      assert.equal((await post("/api/play", { id: "purr" })).code, 202);
      await post("/api/stop");
      await sleep(1500);
      const s = await status();
      assert.deepEqual([s.playing, s.stream === "waking"], [false, false]);
      await post("/api/mock/speaker", { mode: "on" });
    });
    await step("wake: a speaker that is off gives \"Speaker did not answer\" within 8 s; switched on again, the next tap plays", async () => {
      const a = await phone();
      await post("/api/mock/speaker", { mode: "off" });
      assert.equal((await post("/api/play", { id: "cluck" })).code, 202);
      await sleep(1000);
      assert.equal((await status()).playing, false);
      await until(() => a.last.error === "Speaker did not answer", 8800, "the error pushed to the phone");
      assert.equal(a.last.playing, false);
      await post("/api/mock/speaker", { mode: "on" });
      assert.equal((await post("/api/play", { id: "cluck" })).code, 200, "ready speaker plays at once");
      assert.equal((await status()).error, "", "the error clears on the next try");
      await post("/api/stop");
    });
    await step("reconnect: forget stops a playing call, a tap is then 409, and a nameless speaker can be used by address", async () => {
      await post("/api/play", { id: "cluck", loop: "1" });
      await post("/api/bt/forget");
      let s = await status();
      assert.deepEqual([s.playing, s.bt, s.speaker], [false, "idle", ""]);
      assert.equal((await post("/api/play", { id: "cluck" })).code, 409);
      assert.equal((await post("/api/bt/connect", { addr: "", name: "" })).code, 400);
      assert.equal((await post("/api/bt/connect", { addr: "00:1A:7D:DA:71:13" })).code, 200);
      s = await status();
      assert.equal(s.bt, "connecting");
      assert.equal(s.speaker, "00:1A:7D:DA:71:13", "shown by its address");
      await until(async () => (await status()).stream === "ready", 4000, "connected and ready");
      assert.equal((await post("/api/play", { id: "putt" })).code, 200);
      await post("/api/stop");
      await post("/api/bt/connect", { addr: "5C:49:7D:1A:20:8E", name: "Level Box Slim" });
      await until(async () => (await status()).bt === "connected", 4000, "back on the Level Box");
    });
    await step("reconnect: speaker off then on again, and a tap plays without any other step", async () => {
      await post("/api/mock/speaker", { mode: "off" });
      let s = await status();
      assert.deepEqual([s.bt, s.stream], ["idle", "down"]);
      await post("/api/mock/speaker", { mode: "on" });
      s = await status();
      assert.deepEqual([s.bt, s.stream], ["connected", "ready"]);
      assert.equal((await post("/api/play", { id: "putt" })).code, 200);
      await post("/api/stop");
    });
    console.log("note - not covered on the mock: a second tap keeps the first tap's 8 s deadline, the 250 ms pre-roll, the 5 min idle pause, the 3 s / 10 s retry timing (board only: B3, B17)");
  }

  // ---- Phone upload (parked: deferred in PR #12; the mock still implements it) ----
  const WHY = "phone upload deferred, PR #12";
  const upload = async (name, size) => {
    const fd = new FormData();
    fd.append("file", new Blob([Buffer.alloc(size, 1)]), name);
    const r = await fetch(base + "/api/sounds", { method: "POST", body: fd });
    return { code: r.status, body: await r.json() };
  };
  await parked("upload adds a sound, it plays, delete removes it", WHY, async () => {
    assert.equal((await upload("owl_hoot.wav", 8000)).code, 200);
    const list = await get("/api/sounds");
    assert.equal(list.length, 13);
    assert.equal((await post("/api/play", { id: "owl_hoot" })).code, 200);
    await post("/api/stop");
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 200);
    assert.equal((await get("/api/sounds")).length, 12);
  });
  await parked("upload and delete are refused with 409 while a call plays", WHY, async () => {
    await post("/api/play", { id: "assembly_yelp", loop: "1" });
    assert.equal((await upload("x.wav", 4000)).code, 409);
    assert.equal((await post("/api/sounds/delete", { id: "cluck" })).code, 409);
    await post("/api/stop");
    await post("/api/repeat", { on: "0" });
  });
  await parked("upload rules: bad name 400, full 413, same name replaces, unknown delete 404", WHY, async () => {
    assert.equal((await upload("Bad Name.wav", 4000)).code, 400);
    assert.equal((await upload("big.wav", 2 * 1024 * 1024)).code, 413);
    assert.equal((await upload("cluck.wav", 9000)).code, 200);
    assert.equal((await get("/api/sounds")).length, 12);
    assert.equal((await post("/api/sounds/delete", { id: "nope" })).code, 404);
  });
} finally {
  for (const ws of sockets) ws.close();
  server?.kill();
}
if (failures.length) console.error(`\n${failures.length} failed:\n- ` + failures.join("\n- "));
process.exit(failures.length ? 1 : 0);
