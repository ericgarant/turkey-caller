// API contract test: the contract the page relies on and the firmware must keep.
//   node test/api.mjs                         starts the mock and runs against it (npm run test:api)
//   node test/api.mjs http://192.168.4.1      runs against the board; read-only and play/stop/volume steps only
//   DESTRUCTIVE=1 node test/api.mjs <url>     also runs steps that forget the speaker, change config, upload and delete
// Every step runs; failures are listed at the end, so one mismatch does not hide the others.
import { spawn } from "node:child_process";
import assert from "node:assert/strict";

let base = process.argv[2], server;
const onMock = !base;
if (onMock) {
  server = spawn(process.execPath, ["mock/server.mjs", "8093"], { stdio: ["ignore", "pipe", "inherit"] });
  await new Promise((r) => server.stdout.once("data", r));
  base = "http://localhost:8093";
}
const destructive = onMock || process.env.DESTRUCTIVE === "1";

const get = async (p) => (await fetch(base + p)).json();
const post = async (p, form = {}) => {
  const r = await fetch(base + p, { method: "POST", body: new URLSearchParams(form) });
  return { code: r.status, body: await r.json() };
};
const upload = async (name, size) => {
  const fd = new FormData();
  fd.append("file", new Blob([Buffer.alloc(size, 1)]), name);
  const r = await fetch(base + "/api/sounds", { method: "POST", body: fd });
  return { code: r.status, body: await r.json() };
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const failures = [];
const step = async (name, fn, { risky = false } = {}) => {
  if (risky && !destructive) return console.log("skip -", name, "(set DESTRUCTIVE=1 to run on the board)");
  try { await fn(); console.log("ok -", name); } catch (e) { failures.push(name); console.error("FAIL -", name + ":", e.message); }
};
try {
  await step("status has every field the page reads", async () => {
    const s = await get("/api/status");
    for (const k of ["bt", "speaker", "playing", "current", "loop", "repeat", "pos_ms", "len_ms", "volume", "heap", "minHeap",
      "fsFree", "fsTotal", "clients", "uptime", "underruns", "version"]) assert.ok(k in s, `missing ${k}`);
    assert.ok(["idle", "searching", "connecting", "connected"].includes(s.bt));
  });
  await step("12 sounds, each with id, name, group, bytes, ms", async () => {
    const list = await get("/api/sounds");
    assert.equal(list.length, 12);
    for (const s of list) {
      assert.match(s.id, /^[a-z0-9_]{1,24}$/);
      assert.ok(["soft", "loud", "care"].includes(s.group));
      assert.ok(s.bytes > 0 && s.ms > 0);
    }
  });
  await step("play, replace on a new play, stop", async () => {
    assert.equal((await post("/api/play", { id: "cluck", loop: "0" })).code, 200);
    assert.equal((await get("/api/status")).current, "cluck");
    assert.equal((await post("/api/play", { id: "purr", loop: "1" })).code, 200);
    const s = await get("/api/status");
    assert.deepEqual([s.current, s.playing, s.loop], ["purr", true, true]);
    await post("/api/stop");
    assert.equal((await get("/api/status")).playing, false);
  });
  await step("unknown sound id is 400, not a crash", async () => {
    const r = await post("/api/play", { id: "nope" });
    assert.equal(r.code, 400);
    assert.equal(r.body.ok, false);
    assert.ok(r.body.error);
  });
  await step("a call ends by itself", async () => {
    await post("/api/play", { id: "putt", loop: "0" }); // 1.2 s
    await sleep(1600);
    assert.equal((await get("/api/status")).playing, false);
  });
  await step("Repeat is one switch on the board for every phone; needs on", async () => {
    const before = (await get("/api/status")).repeat;
    assert.equal((await post("/api/repeat", { on: "1" })).code, 200);
    assert.equal((await get("/api/status")).repeat, true);
    assert.equal((await post("/api/repeat", { on: "0" })).code, 200);
    assert.equal((await get("/api/status")).repeat, false);
    assert.equal((await post("/api/repeat")).code, 400);
    await post("/api/repeat", { on: before ? "1" : "0" });
  });
  await step("volume clamps to 0..100 and needs a level", async () => {
    const before = (await get("/api/status")).volume; // put it back afterwards, also on the board
    await post("/api/volume", { level: "150" });
    assert.equal((await get("/api/status")).volume, 100);
    await post("/api/volume", { level: "-20" });
    assert.equal((await get("/api/status")).volume, 0);
    assert.equal((await post("/api/volume")).code, 400);
    await post("/api/volume", { level: String(before) });
  });
  await step("upload and delete are refused while playing (409)", async () => {
    await post("/api/play", { id: "assembly_yelp", loop: "1" });
    assert.equal((await upload("x.wav", 4000)).code, 409);
    assert.equal((await post("/api/sounds/delete", { id: "cluck" })).code, 409);
    await post("/api/stop");
  }, { risky: true });
  await step("upload rules: bad name 400, too big 413, ok then delete, unknown delete 404", async () => {
    assert.equal((await upload("Bad Name.wav", 4000)).code, 400);
    assert.equal((await upload("big.wav", 2 * 1024 * 1024)).code, 413);
    assert.equal((await upload("owl_hoot.wav", 4000)).code, 200);
    assert.equal((await get("/api/sounds")).length, 13);
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 200);
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 404);
    assert.equal((await get("/api/sounds")).length, 12);
  }, { risky: true });
  await step("config validation: short password and long name refused, password never returned", async () => {
    assert.equal((await post("/api/config", { ap_ssid: "Turk", ap_pass: "short" })).code, 400);
    assert.equal((await post("/api/config", { ap_ssid: "x".repeat(33) })).code, 400);
    assert.equal((await post("/api/config", { ap_ssid: "" })).code, 400);
    const c = await get("/api/config");
    assert.ok(!("ap_pass" in c));
  }, { risky: true });
  await step("forget stops playback; play without a speaker is 409; connect needs a name", async () => {
    await post("/api/play", { id: "cluck", loop: "1" });
    await post("/api/bt/forget");
    const s = await get("/api/status");
    assert.deepEqual([s.playing, s.bt], [false, "idle"]);
    assert.equal((await post("/api/play", { id: "cluck" })).code, 409);
    assert.equal((await post("/api/bt/connect", { addr: "5C:49:7D:1A:20:8E", name: "" })).code, 400);
    assert.equal((await post("/api/bt/connect", { addr: "5C:49:7D:1A:20:8E", name: "Level Box Slim" })).code, 200);
    assert.equal((await get("/api/status")).bt, "connecting");
    await sleep(2800);
    assert.equal((await get("/api/status")).bt, "connected");
  }, { risky: true });
  await step("unknown API path is a JSON 404", async () => {
    const r = await fetch(base + "/api/nope");
    assert.equal(r.status, 404);
    assert.equal((await r.json()).ok, false);
  });
} finally {
  server?.kill();
}
if (failures.length) console.error(`\n${failures.length} failed:\n- ` + failures.join("\n- "));
process.exit(failures.length ? 1 : 0);
