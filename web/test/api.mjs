// API contract test against the mock (the same contract the firmware must keep).
// Run: npm run test:api (from web/). No browser needed.
import { spawn } from "node:child_process";
import assert from "node:assert/strict";

const PORT = 8093, base = `http://localhost:${PORT}`;
const server = spawn(process.execPath, ["mock/server.mjs", String(PORT)], { stdio: ["ignore", "pipe", "inherit"] });
await new Promise((r) => server.stdout.once("data", r));

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

let failed = false;
const step = async (name, fn) => { await fn(); console.log("ok -", name); };
try {
  await step("status has every field the page reads", async () => {
    const s = await get("/api/status");
    for (const k of ["bt", "speaker", "playing", "current", "loop", "pos_ms", "len_ms", "volume", "heap", "minHeap",
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
  await step("volume clamps to 0..100 and needs a level", async () => {
    await post("/api/volume", { level: "150" });
    assert.equal((await get("/api/status")).volume, 100);
    await post("/api/volume", { level: "-20" });
    assert.equal((await get("/api/status")).volume, 0);
    assert.equal((await post("/api/volume")).code, 400);
    await post("/api/volume", { level: "60" });
  });
  await step("upload and delete are refused while playing (409)", async () => {
    await post("/api/play", { id: "assembly_yelp", loop: "1" });
    assert.equal((await upload("x.mp3", 4000)).code, 409);
    assert.equal((await post("/api/sounds/delete", { id: "cluck" })).code, 409);
    await post("/api/stop");
  });
  await step("upload rules: bad name 400, too big 413, ok then delete, unknown delete 404", async () => {
    assert.equal((await upload("Bad Name.mp3", 4000)).code, 400);
    assert.equal((await upload("big.mp3", 2 * 1024 * 1024)).code, 413);
    assert.equal((await upload("owl_hoot.mp3", 4000)).code, 200);
    assert.equal((await get("/api/sounds")).length, 13);
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 200);
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 404);
    assert.equal((await get("/api/sounds")).length, 12);
  });
  await step("config validation: short password and long name refused, password never returned", async () => {
    assert.equal((await post("/api/config", { ap_ssid: "Turk", ap_pass: "short" })).code, 400);
    assert.equal((await post("/api/config", { ap_ssid: "x".repeat(33) })).code, 400);
    assert.equal((await post("/api/config", { ap_ssid: "" })).code, 400);
    const c = await get("/api/config");
    assert.ok(!("ap_pass" in c));
  });
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
  });
  await step("unknown API path is a JSON 404", async () => {
    const r = await fetch(base + "/api/nope");
    assert.equal(r.status, 404);
    assert.equal((await r.json()).ok, false);
  });
} catch (e) {
  failed = true;
  console.error("FAIL -", e.message);
} finally {
  server.kill();
}
process.exit(failed ? 1 : 0);
