// Rapid-tap stress test. Same script for the mock and the real board.
//   node test/stress.mjs                          starts the mock and runs against it
//   node test/stress.mjs http://192.168.4.1       runs against the board (join its Wi-Fi first)
// Options (env): TAPS=50  GAP_MS=150 (the firmware page'"'"'s own stress test uses 150 ms)
// Soak: TAPS=360 GAP_MS=20000 node test/stress.mjs http://192.168.4.1   (2 hours, calls in order)
// Pass = 0 failed requests, 0 new underruns during the run, no reboot, lowest heap since boot >= 40 KB
// (docs/architecture.md, Phase 0).
import { spawn } from "node:child_process";

const TAPS = +(process.env.TAPS || 50), GAP = +(process.env.GAP_MS || 150), MIN_HEAP = 40 * 1024;
let base = process.argv[2], server;
if (!base) {
  server = spawn(process.execPath, ["mock/server.mjs", "8092"], { stdio: ["ignore", "pipe", "inherit"] });
  await new Promise((r) => server.stdout.once("data", r));
  base = "http://localhost:8092";
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const get = async (p) => (await fetch(base + p, { signal: AbortSignal.timeout(5000) })).json();
const post = async (p, form = {}) => {
  const r = await fetch(base + p, { method: "POST", body: new URLSearchParams(form), signal: AbortSignal.timeout(5000) });
  return { code: r.status, body: await r.json().catch(() => ({})) };
};

let failed = 0, lowHeap = Infinity, newUnder = 0, rebooted = false, lastUp = 0;
try {
  const sounds = await get("/api/sounds");
  const ids = sounds.map((s) => s.id ?? s);
  const s0 = await get("/api/status");
  if (s0.bt !== "connected") throw new Error(`speaker not connected (bt=${s0.bt}); pair it first`);
  const under0 = s0.underruns ?? 0; // underruns is a count since boot; only new ones count
  lastUp = s0.uptime ?? 0;
  for (let i = 0; i < TAPS; i++) {
    const r = await post("/api/play", { id: ids[i % ids.length], loop: "0" }).catch((e) => ({ code: 0, body: { error: String(e) } }));
    if (r.code !== 200) { failed++; console.log(`tap ${i + 1} failed: ${r.code} ${r.body.error || ""}`); }
    const s = await get("/api/status").catch(() => null);
    if (s) {
      lowHeap = Math.min(lowHeap, s.heap, s.minHeap ?? Infinity);
      newUnder = Math.max(newUnder, (s.underruns ?? 0) - under0);
      if ((s.uptime ?? 0) < lastUp) { rebooted = true; console.log(`tap ${i + 1}: board rebooted during the test (uptime ${lastUp} -> ${s.uptime})`); }
      lastUp = s.uptime ?? lastUp;
    }
    await sleep(GAP);
  }
  await post("/api/stop");
} catch (e) {
  failed++;
  console.error("ERROR -", e.message);
}
const pass = failed === 0 && newUnder === 0 && !rebooted && lowHeap >= MIN_HEAP;
console.log(`taps=${TAPS} failed=${failed} newUnderruns=${newUnder} rebooted=${rebooted} lowestHeapSinceBoot=${lowHeap} (need >= ${MIN_HEAP})`);
console.log(pass ? "PASS" : "FAIL");
server?.kill();
process.exit(pass ? 0 : 1);
