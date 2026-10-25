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
// A mono IMA ADPCM WAV at 22.05 kHz of about `size` bytes (silence), the format the page uploads (F03).
const adpcmWav = (size, { format = 0x11, rate = 22050 } = {}) => {
  const blocks = Math.max(1, Math.round((size - 60) / 256)), b = Buffer.alloc(60 + blocks * 256);
  b.write("RIFF", 0); b.writeUInt32LE(b.length - 8, 4); b.write("WAVE", 8); b.write("fmt ", 12); b.writeUInt32LE(20, 16);
  b.writeUInt16LE(format, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(rate, 24); b.writeUInt32LE(Math.round(rate * 256 / 505), 28);
  b.writeUInt16LE(256, 32); b.writeUInt16LE(format === 0x11 ? 4 : 16, 34); b.writeUInt16LE(2, 36); b.writeUInt16LE(505, 38);
  b.write("fact", 40); b.writeUInt32LE(4, 44); b.writeUInt32LE(blocks * 505, 48); b.write("data", 52); b.writeUInt32LE(blocks * 256, 56);
  return b;
};
const upload = async (name, size, opts) => {
  const fd = new FormData();
  fd.append("file", new Blob([opts === "junk" ? Buffer.alloc(size, 1) : adpcmWav(size, opts)]), name);
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
      "fsFree", "fsTotal", "clients", "uptime", "underruns", "version", "stream", "error"]) assert.ok(k in s, `missing ${k}`);
    assert.ok(["idle", "searching", "connecting", "connected"].includes(s.bt));
    assert.ok(["ready", "waking", "suspended", "down"].includes(s.stream));
  });
  await step("16 sounds (11 + the Dindon pack), each with id, name, group, bytes, ms", async () => {
    const list = await get("/api/sounds");
    assert.equal(list.length, 16);
    for (const s of list) {
      assert.match(s.id, /^[a-z0-9_]{1,24}$/);
      assert.ok(["soft", "loud", "care", "pack"].includes(s.group));
      assert.ok(s.bytes > 0 && s.ms > 0);
      assert.equal(s.animal, "turkey");
    }
  });
  await step("a deer pack on the card adds 6 deer calls; pulling it removes them", async () => {
    await post("/api/mock/pack", { name: "deer", on: "1" });
    const list = await get("/api/sounds");
    const deer = list.filter((s) => s.animal === "deer");
    assert.equal(deer.length, 6);
    assert.ok(deer.every((s) => s.group === "deer" && s.hint && s.ms > 0 && s.bytes > 0));
    assert.ok(deer.some((s) => s.ms > 10000), "pack clips are not capped at 10 s");
    await post("/api/mock/pack", { name: "deer", on: "0" });
    assert.equal((await get("/api/sounds")).length, 16);
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
  // Uploads go to the SD card (F03 on F12). The mock needs its card on; the board needs one in.
  if (onMock) await post("/api/mock/sd", { on: "1" });
  await step("upload rules: bad name 400, not ADPCM 400, built-in id 409, ok then replace, delete, unknown 404", async () => {
    assert.equal((await upload("Bad Name.wav", 4000)).code, 400);
    assert.equal((await upload("junk.wav", 4000, "junk")).code, 400);
    assert.equal((await upload("pcm.wav", 4000, { format: 1 })).code, 400);
    assert.equal((await upload("fast.wav", 4000, { rate: 44100 })).code, 400);
    assert.equal((await upload("cluck.wav", 4000)).code, 409);
    const before = (await get("/api/sounds")).length;
    assert.equal((await upload("owl_hoot.wav", 4000)).code, 200);
    let list = await get("/api/sounds");
    assert.equal(list.length, before + 1);
    assert.equal(list.find((s) => s.id === "owl_hoot").group, "mine");
    assert.equal((await upload("owl_hoot.wav", 6000)).code, 200);
    assert.equal((await get("/api/sounds")).length, before + 1);
    assert.equal((await post("/api/sounds/delete", { id: "cluck" })).code, 400);
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 200);
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 404);
    assert.equal((await get("/api/sounds")).length, before);
  }, { risky: true });
  await step("an upload while a built-in plays is fine; one that does not fit gets 413", async () => {
    await post("/api/play", { id: "assembly_yelp", loop: "1" });
    assert.equal((await upload("owl_hoot.wav", 4000)).code, 200);
    await post("/api/stop");
    await post("/api/repeat", { on: "0" });
    assert.equal((await post("/api/sounds/delete", { id: "owl_hoot" })).code, 200);
    if (onMock) {
      await post("/api/mock/sd", { free: "40000" });
      assert.equal((await upload("big.wav", 20000)).code, 413);
    }
  }, { risky: true });
  if (onMock) await post("/api/mock/sd", { on: "" });
  await step("LED on motion (F13): in config, 0 or 1 only, saved, motion fields in status", async () => {
    const c = await get("/api/config");
    assert.ok(c.led_motion === 0 || c.led_motion === 1);
    const s = await get("/api/status");
    assert.equal(typeof s.motion, "boolean");
    assert.equal(typeof s.motionAt, "number");
    assert.equal((await post("/api/config", { led_motion: "2" })).code, 400);
    assert.equal((await post("/api/config", { led_motion: c.led_motion ? "0" : "1", rev: String(c.rev) })).code, 200);
    const c2 = await get("/api/config");
    assert.equal(c2.led_motion, c.led_motion ? 0 : 1);
    assert.notEqual(c2.rev, c.rev);
    assert.equal((await post("/api/config", { led_motion: "1", rev: String(c.rev) })).code, 409);
    assert.equal((await post("/api/config", { led_motion: String(c.led_motion), rev: String(c2.rev) })).code, 200);
  }, { risky: true });
  await step("config validation: short password and long name refused, password never returned", async () => {
    assert.equal((await post("/api/config", { ap_ssid: "Turk", ap_pass: "short" })).code, 400);
    assert.equal((await post("/api/config", { ap_ssid: "x".repeat(33) })).code, 400);
    assert.equal((await post("/api/config", { ap_ssid: "" })).code, 400);
    const c = await get("/api/config");
    assert.ok(!("ap_pass" in c));
  }, { risky: true });
  await step("config sync (F05): rev in config and status, stale rev refused, name-only change needs no restart", async () => {
    const c = await get("/api/config");
    assert.equal(typeof c.rev, "number");
    assert.equal(typeof c.default_pass, "boolean");
    assert.equal((await get("/api/status")).cfg, c.rev);
    const stale = await post("/api/config", { ap_ssid: c.ap_ssid, device_name: "Stale phone", rev: c.rev - 1 });
    assert.equal(stale.code, 409);
    assert.equal((await get("/api/config")).device_name, c.device_name, "nothing saved on 409");
    assert.equal((await post("/api/config", { ap_ssid: c.ap_ssid, device_name: "x".repeat(25), rev: c.rev })).code, 400);
    assert.equal((await post("/api/config", { ap_ssid: c.ap_ssid, device_name: "é".repeat(13), rev: c.rev })).code, 400, "limits are bytes");
    assert.equal((await post("/api/config", { ap_ssid: c.ap_ssid, ap_pass: "motdepasseé", rev: c.rev })).code, 400, "password is ASCII");
    const r = await post("/api/config", { ap_ssid: c.ap_ssid, device_name: "Ridge blind", rev: c.rev });
    assert.deepEqual(r.body, { ok: true, restart: false });
    const c2 = await get("/api/config");
    assert.equal(c2.device_name, "Ridge blind");
    assert.notEqual(c2.rev, c.rev);
    assert.equal((await get("/api/status")).cfg, c2.rev);
    await post("/api/config", { ap_ssid: c.ap_ssid, device_name: c.device_name, rev: c2.rev });
  }, { risky: true });
  if (onMock) {
    await step("a tap on a sleeping speaker answers 202, wakes it, then plays (F07)", async () => {
      await post("/api/mock/speaker", { mode: "asleep" });
      const r = await post("/api/play", { id: "cluck", loop: "0" });
      assert.deepEqual([r.code, r.body.waking], [202, true]);
      assert.equal((await get("/api/status")).stream, "waking");
      await sleep(1300);
      const s = await get("/api/status");
      assert.deepEqual([s.stream, s.playing, s.current], ["ready", true, "cluck"]);
      await post("/api/stop");
    });
    await step("Stop while waking cancels the call", async () => {
      await post("/api/mock/speaker", { mode: "asleep" });
      assert.equal((await post("/api/play", { id: "purr" })).code, 202);
      await post("/api/stop");
      await sleep(1300);
      const s = await get("/api/status");
      assert.deepEqual([s.playing, s.stream === "waking"], [false, false]);
      await post("/api/mock/speaker", { mode: "on" });
    });
    await step("a speaker that is off: 202, then \"Speaker did not answer\" within 8 s", async () => {
      await post("/api/mock/speaker", { mode: "off" });
      assert.equal((await post("/api/play", { id: "cluck" })).code, 202);
      await sleep(8300);
      const s = await get("/api/status");
      assert.deepEqual([s.playing, s.error], [false, "Speaker did not answer"]);
      await post("/api/mock/speaker", { mode: "on" });
    });
  }
  await step("forget stops playback; play without a speaker is 409; connect by address", async () => {
    await post("/api/play", { id: "cluck", loop: "1" });
    await post("/api/bt/forget");
    const s = await get("/api/status");
    assert.deepEqual([s.playing, s.bt], [false, "idle"]);
    assert.equal((await post("/api/play", { id: "cluck" })).code, 409);
    assert.equal((await post("/api/bt/connect", { addr: "", name: "" })).code, 400);
    assert.equal((await post("/api/bt/connect", { addr: "5C:49:7D", name: "Level Box Slim" })).code, 400);
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
