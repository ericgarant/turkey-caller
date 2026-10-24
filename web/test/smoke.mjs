// Drives the page against the mock server in a phone-sized Chromium.
// Run: npm test (from web/). Needs Playwright: npm install
import { spawn } from "node:child_process";
import { chromium } from "playwright";
import assert from "node:assert/strict";

const PORT = 8091;
const server = spawn(process.execPath, ["mock/server.mjs", String(PORT)], { stdio: ["ignore", "pipe", "inherit"] });
await new Promise((r) => server.stdout.once("data", r));

const browser = await chromium.launch();
const errors = [];
let failed = false;
try {
  const page = await browser.newPage({ viewport: { width: 375, height: 740 } });
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(`http://localhost:${PORT}/`);

  const step = async (name, fn) => { await fn(); console.log("ok -", name); };

  await step("shows the 12 calls in three groups", async () => {
    await page.waitForSelector(".call");
    assert.equal(await page.locator(".call").count(), 12);
    assert.deepEqual(await page.locator("#groups h2").allTextContents(), ["Soft calls", "Louder calls", "Use with care"]);
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
    await page.waitForSelector('#devList [data-i]', { timeout: 8000 });
    await page.click('#devList [data-i="0"]');
    await page.waitForFunction(() => document.getElementById("spkText").textContent === "Level Box Slim", null, { timeout: 8000 });
  });
  await step("settings show sounds, storage and Wi-Fi name", async () => {
    await page.click('nav [data-go="settings"]');
    assert.equal(await page.locator("#sndList li").count(), 12);
    assert.match(await page.textContent("#fsText"), /used of 1408 KB/);
    assert.equal(await page.inputValue("#apSsid"), "TurkeyCaller");
  });
  await step("upload adds a sound, delete removes it", async () => {
    await page.setInputFiles("#upFile", { name: "Owl Hoot.mp3", mimeType: "audio/mpeg", buffer: Buffer.alloc(16000, 1) });
    await page.waitForFunction(() => document.querySelectorAll("#sndList li").length === 13);
    assert.ok(await page.isVisible('[data-del="owl_hoot"]'));
    await page.click('[data-del="owl_hoot"]');
    await page.click('[data-del="owl_hoot"]');
    await page.waitForFunction(() => document.querySelectorAll("#sndList li").length === 12);
  });
  await step("short Wi-Fi password is refused before sending", async () => {
    await page.fill("#apPass", "short");
    await page.click("#cfgSave");
    await page.waitForFunction(() => /8 characters/.test(document.getElementById("toast").textContent));
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
