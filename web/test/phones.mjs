// Two phones on one mock board, in phone-sized Chromium: Repeat, volume and playing state stay in
// step, and the page recovers from the board's busy (503) window.
// Run: npm run test:phones (from web/). Needs Playwright: npm install
import { spawn } from "node:child_process";
import { chromium } from "playwright";
import assert from "node:assert/strict";

const PORT = 8094, base = `http://localhost:${PORT}`;
const server = spawn(process.execPath, ["mock/server.mjs", String(PORT)], { stdio: ["ignore", "pipe", "inherit"] });
await new Promise((r) => server.stdout.once("data", r));
const busy = (count) => fetch(base + "/api/mock/busy", { method: "POST", body: new URLSearchParams({ count: String(count) }) });
const apiStatus = async () => (await fetch(base + "/api/status")).json();

const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || undefined });
const errors = [];
let failed = false;
try {
  const open = async () => {
    const ctx = await browser.newContext({ viewport: { width: 375, height: 740 } });
    const page = await ctx.newPage();
    page.on("pageerror", (e) => errors.push(e.message));
    await page.goto(base + "/");
    await page.waitForSelector(".call");
    await page.waitForFunction(() => document.getElementById("spkText").textContent === "Level Box Slim");
    return page;
  };
  const a = await open(), b = await open();
  const step = async (name, fn) => { await fn(); console.log("ok -", name); };

  await step("a call played on one phone shows as playing on the other, and Stop on either ends it", async () => {
    await a.click('.call[data-id="purr"]');
    await b.waitForSelector('.call.on[data-id="purr"]', { timeout: 2000 });
    await b.click("#stopBtn");
    await a.waitForFunction(() => !document.querySelector(".call.on"), null, { timeout: 2000 });
  });
  await step("Repeat on one phone turns it on for the other", async () => {
    await a.check("#loopSw");
    await b.waitForFunction(() => document.getElementById("loopSw").checked, null, { timeout: 2000 });
    assert.equal((await apiStatus()).repeat, true);
  });
  await step("a call tapped with Repeat on keeps looping on both phones; Stop ends it", async () => {
    await b.click('.call[data-id="putt"]'); // 1.2 s, so a second pass proves the loop
    await a.waitForSelector('.call.on[data-id="putt"]', { timeout: 2000 });
    await a.waitForTimeout(1800);
    assert.equal(await a.locator('.call.on[data-id="putt"]').count(), 1);
    assert.equal(await b.locator('.call.on[data-id="putt"]').count(), 1);
    await a.click("#stopBtn");
    await b.waitForFunction(() => !document.querySelector(".call.on"), null, { timeout: 2000 });
  });
  await step("a phone that reloads while Repeat is on and a loop plays comes back showing both", async () => {
    await a.click('.call[data-id="cluck"]');
    await b.reload();
    await b.waitForSelector('.call.on[data-id="cluck"]', { timeout: 3000 });
    assert.equal(await b.isChecked("#loopSw"), true);
    await b.click("#stopBtn");
    await a.uncheck("#loopSw");
    await b.waitForFunction(() => !document.getElementById("loopSw").checked, null, { timeout: 2000 });
  });
  await step("volume moved on one phone reaches the other within 1 s", async () => {
    const start = (await apiStatus()).volume;
    await a.click("#volUp");
    await a.waitForFunction(async (v) => (await (await fetch("/api/status")).json()).volume > v, start);
    const up = (await apiStatus()).volume;
    await b.waitForFunction((v) => document.getElementById("vol").value === String(v), up, { timeout: 1000 });
    await b.waitForTimeout(1100); // a phone ignores pushed volume for 1 s after its own change (index.html, paint)
    await b.click("#volDn");
    await a.waitForFunction(async (v) => (await (await fetch("/api/status")).json()).volume < v, up);
    const down = (await apiStatus()).volume;
    await a.waitForFunction((v) => document.getElementById("vol").value === String(v), down, { timeout: 1000 });
  });
  await step("busy window: a tap is retried by the page and the call plays", async () => {
    await busy(2);
    await a.click('.call[data-id="whine"]');
    await a.waitForSelector('.call.on[data-id="whine"]', { timeout: 6000 });
    await b.waitForSelector('.call.on[data-id="whine"]', { timeout: 2000 });
    await a.click("#stopBtn");
    await a.waitForFunction(() => !document.querySelector(".call.on"));
  });
  await step("busy window while the page loads: the calls still appear", async () => {
    await busy(3);
    const c = await open();
    assert.equal(await c.locator(".call").count(), 12);
    await c.context().close();
  });
  await step("busy for longer than the page waits: it says so, nothing plays, and the next tap works", async () => {
    await busy(7); // more than the page's retries on one request
    await a.click('.call[data-id="cluck"]');
    await a.waitForFunction(() => /busy/i.test(document.getElementById("toast").textContent), null, { timeout: 12000 });
    assert.equal((await apiStatus()).playing, false);
    await busy(0);
    await a.click('.call[data-id="cluck"]');
    await a.waitForSelector('.call.on[data-id="cluck"]', { timeout: 3000 });
    await a.click("#stopBtn");
  });
  assert.deepEqual(errors, [], "page errors: " + errors.join("; "));
} catch (e) {
  failed = true;
  console.error("FAIL -", e.message);
} finally {
  await browser.close();
  server.kill();
}
process.exit(failed ? 1 : 0);
