// Drives the page against the mock server in a phone-sized Chromium.
// Run: npm test (from web/). Needs Playwright: npm install
import { spawn } from "node:child_process";
import { chromium } from "playwright";
import assert from "node:assert/strict";

const PORT = 8091;
const server = spawn(process.execPath, ["mock/server.mjs", String(PORT)], { stdio: ["ignore", "pipe", "inherit"] });
await new Promise((r) => server.stdout.once("data", r));

const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || undefined });
const errors = [];
let failed = false;
try {
  const page = await browser.newPage({ viewport: { width: 375, height: 740 } });
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(`http://localhost:${PORT}/`);

  const step = async (name, fn) => { await fn(); console.log("ok -", name); };

  await step("shows the 16 calls in four groups", async () => {
    await page.waitForSelector(".call");
    assert.equal(await page.locator(".call").count(), 16);
    assert.deepEqual(await page.locator("#groups h2").allTextContents(), ["Soft calls", "Louder calls", "Use with care", "Dindon pack"]);
  });
  await step("animal tabs: Moose is empty with a hint; a deer pack adds a Deer tab with 6 calls", async () => {
    const pack = (name, on) => page.request.post(`http://localhost:${PORT}/api/mock/pack`, { form: { name, on } });
    assert.deepEqual(await page.locator("#tabs button").allTextContents(), ["Turkey", "Moose"]);
    await page.click('#tabs [data-animal="moose"]');
    assert.match(await page.locator("#groups").innerText(), /SD card/);
    await pack("deer", "1");
    await page.reload();
    await page.waitForSelector("#tabs");
    assert.deepEqual(await page.locator("#tabs button").allTextContents(), ["Turkey", "Moose", "Deer"]);
    await page.click('#tabs [data-animal="deer"]');
    assert.equal(await page.locator(".call").count(), 6);
    assert.deepEqual(await page.locator("#groups h2").allTextContents(), ["Deer calls"]);
    await page.reload();
    await page.waitForSelector(".call");
    assert.equal(await page.locator("#tabs [aria-selected=true]").getAttribute("data-animal"), "deer");  // remembered
    await pack("moose", "1");
    await page.reload();
    await page.waitForSelector(".call");
    await page.click('#tabs [data-animal="moose"]');
    assert.deepEqual(await page.locator("#groups h2").allTextContents(), ["Moose calls"]);
    await page.click('#tabs [data-animal="turkey"]');
    assert.equal(await page.locator(".call").count(), 16);
    await pack("deer", "0"); await pack("moose", "0");
    await page.reload();
    await page.waitForSelector(".call");
  });
  await step("storage: card meter, then the no-card message and an \"SD card removed\" toast", async () => {
    const api = (on) => page.request.post(`http://localhost:${PORT}/api/mock/sd`, { form: { on } });
    await page.click('nav [data-go="settings"]');
    await api("1");
    await page.waitForFunction(() => /5120 KB used of 960 MB/.test(document.getElementById("fsText").textContent));
    assert.equal(await page.locator("#noSd").isVisible(), false);
    await api("0");
    await page.waitForFunction(() => !document.getElementById("noSd").hidden);
    assert.match(await page.locator("#noSd").innerText(), /No SD card: recordings unavailable/);
    await page.waitForFunction(() => /SD card removed/.test(document.getElementById("toast").textContent));
    await api("");  // back to a board that reports flash only (the page keeps old fields until a reload)
    await page.reload();
    await page.waitForSelector(".call");
    await page.click('nav [data-go="settings"]');
    await page.waitForFunction(() => document.getElementById("noSd").hidden && /KB used of 1408 KB/.test(document.getElementById("fsText").textContent));
    await page.click('nav [data-go="calls"]');
  });
  await step("speaker pill shows the connected speaker", async () => {
    await page.waitForFunction(() => document.getElementById("spkText").textContent === "Level Box Slim");
  });
  await step("tapping a call plays it, Stop stops it", async () => {
    await page.click('.call[data-id="assembly_yelp"]');
    await page.waitForSelector('.call.on[data-id="assembly_yelp"]');
    assert.equal(await page.isEnabled("#stopBtn"), true);
    await page.click("#stopBtn");
    await page.waitForFunction(() => !document.querySelector(".call.on"));
  });
  await step("tapping a playing call stops it", async () => {
    await page.click('.call[data-id="cutting"]');
    await page.waitForSelector('.call.on[data-id="cutting"]');
    await page.click('.call[data-id="cutting"]');
    await page.waitForFunction(() => !document.querySelector(".call.on"));
  });
  await step("a tap on a sleeping speaker says it is waking it, then plays", async () => {
    await page.evaluate(() => fetch("/api/mock/speaker", { method: "POST", body: new URLSearchParams({ mode: "asleep" }) }));
    await page.click('.call[data-id="purr"]');
    await page.waitForFunction(() => /Waking the speaker/.test(document.getElementById("toast").textContent));
    await page.waitForSelector('.call.on[data-id="purr"]', { timeout: 6000 });
    await page.click("#stopBtn");
    await page.waitForFunction(() => !document.querySelector(".call.on"));
  });
  await step("volume buttons change the board's volume", async () => {
    await page.click("#volUp");
    await page.waitForFunction(async () => (await (await fetch("/api/status")).json()).volume === 65);
  });
  await step("no sideways scrolling at phone width", async () => {
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth), 375);
  });
  await step("forget, search and use a speaker", async () => {
    await page.click('nav [data-go="speaker"]');
    await page.click("#forgetBtn");
    await page.click("#forgetBtn");
    await page.waitForFunction(() => document.getElementById("spkText").textContent === "No speaker");
    await page.click('nav [data-go="calls"]');
    assert.equal(await page.isVisible("#noSpk"), true);
    assert.equal(await page.isDisabled('.call[data-id="cluck"]'), true);
    await page.click('nav [data-go="speaker"]');
    await page.click("#scanBtn");
    await page.waitForFunction(() => document.querySelectorAll("#devList li .mono").length === 4, null, { timeout: 12000 });
    assert.equal(await page.locator("#devList [data-i]").count(), 4, "a nameless speaker is usable by address (F06)");
    await page.click('#devList [data-i="0"]');
    await page.waitForFunction(() => document.getElementById("spkText").textContent === "Level Box Slim", null, { timeout: 8000 });
  });
  await step("settings show sounds, storage and Wi-Fi name", async () => {
    await page.click('nav [data-go="settings"]');
    assert.equal(await page.locator("#sndList li").count(), 16);
    assert.match(await page.textContent("#fsText"), /used of 1408 KB/);
    assert.equal(await page.inputValue("#apSsid"), "TurkeyCaller");
  });
  await step("with no SD card, adding sounds is hidden and nothing can be deleted", async () => {
    assert.equal(await page.isVisible("#upFile"), false);
    assert.equal(await page.locator("#sndList [data-del]").count(), 0);
  });
  await step("with a card, a WAV from the phone is converted, uploaded, listed as mine, then deleted (F03)", async () => {
    await page.evaluate(() => fetch("/api/mock/sd", { method: "POST", body: new URLSearchParams({ on: "1" }) }));
    await page.waitForSelector("#upFile", { state: "visible", timeout: 8000 });
    // 1 s of 440 Hz, 16-bit PCM stereo at 44.1 kHz: the page must turn it into mono ADPCM at 22.05 kHz.
    const n = 44100, b = Buffer.alloc(44 + n * 4);
    b.write("RIFF", 0); b.writeUInt32LE(36 + n * 4, 4); b.write("WAVEfmt ", 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22);
    b.writeUInt32LE(44100, 24); b.writeUInt32LE(44100 * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write("data", 36); b.writeUInt32LE(n * 4, 40);
    for (let i = 0; i < n; i++) { const v = Math.round(12000 * Math.sin(2 * Math.PI * 440 * i / 44100)); b.writeInt16LE(v, 44 + i * 4); b.writeInt16LE(v, 46 + i * 4); }
    await page.setInputFiles("#upFile", { name: "Owl Hoot.wav", mimeType: "audio/wav", buffer: b });
    await page.waitForFunction(() => [...document.querySelectorAll("#sndList .mono")].some((e) => e.textContent.startsWith("owl_hoot.wav")), null, { timeout: 10000 });
    const s = await page.evaluate(async () => (await (await fetch("/api/sounds")).json()).find((x) => x.id === "owl_hoot"));
    assert.equal(s.group, "mine");
    assert.ok(s.bytes > 11000 && s.bytes < 13000, `about 11 KB for 1 s of ADPCM, got ${s.bytes}`);
    assert.equal(await page.locator("#sndList [data-del]").count(), 1);
    await page.click('#sndList [data-del="owl_hoot"]');
    await page.click('#sndList [data-del="owl_hoot"]');
    await page.waitForFunction(() => !document.querySelector('#sndList [data-del="owl_hoot"]'), null, { timeout: 8000 });
    await page.evaluate(() => fetch("/api/mock/sd", { method: "POST", body: new URLSearchParams({ on: "" }) }));
  });
  await step("short Wi-Fi password is refused before sending", async () => {
    await page.fill("#apPass", "short");
    await page.click("#cfgSave");
    await page.waitForFunction(() => /8 to 63 characters/.test(document.querySelector("#fPass .err").textContent));
    await page.fill("#apPass", "");
  });
  await step("instructions page opens from the ? button and covers the BOOT reset", async () => {
    await page.click('[data-go="help"]');
    assert.equal(await page.locator("#s-help").isVisible(), true);
    assert.match(await page.locator("#s-help").innerText(), /BOOT[\s\S]*5 seconds[\s\S]*gobble123/);
    await page.click('nav [data-go="calls"]');
  });
  await step("dawn mode turns on and is remembered", async () => {
    await page.click("#dawnBtn");
    await page.reload();
    assert.equal(await page.evaluate(() => document.body.classList.contains("dawn")), true);
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
