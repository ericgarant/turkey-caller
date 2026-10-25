// Two phones on the Settings screen at once (F05): every change shows on the other phone.
// Run: node test/settings.mjs (from web/), part of npm test.
import { spawn } from "node:child_process";
import { chromium } from "playwright";
import assert from "node:assert/strict";

const PORT = 8096, URL = `http://localhost:${PORT}/`;
const server = spawn(process.execPath, ["mock/server.mjs", String(PORT)], { stdio: ["ignore", "pipe", "inherit"] });
await new Promise((r) => server.stdout.once("data", r));

const browser = await chromium.launch();
const errors = [];
let failed = false;
const phone = async () => {
  const ctx = await browser.newContext({ viewport: { width: 375, height: 740 } });
  const p = await ctx.newPage();
  p.on("pageerror", (e) => errors.push(e.message));
  await p.goto(URL);
  await p.click('nav [data-go="settings"]');
  await p.waitForFunction(() => document.getElementById("apSsid").value === "TurkeyCaller");
  return p;
};
const step = async (name, fn) => { await fn(); console.log("ok -", name); };
const within = (ms) => ({ timeout: ms });

try {
  const a = await phone(), b = await phone();

  await step("factory password reminder shows on both phones", async () => {
    assert.equal(await a.isVisible("#cfgDefPw"), true);
    assert.equal(await b.isVisible("#cfgDefPw"), true);
  });
  await step("Save stays off until something changes", async () => {
    assert.equal(await a.isDisabled("#cfgSave"), true);
    await a.fill("#devName", "Ridge blind");
    assert.equal(await a.isEnabled("#cfgSave"), true);
    assert.equal(await a.textContent("#cfgSave"), "Save");
  });
  await step("a device name change shows on the other phone within 1 s, no restart", async () => {
    await a.click("#cfgSave");
    await b.waitForFunction(() => document.getElementById("devName").value === "Ridge blind", null, within(1000));
    await b.waitForFunction(() => document.getElementById("brand").textContent === "Ridge blind", null, within(1000));
    assert.equal(await a.textContent("#brand"), "Ridge blind");
    assert.equal(await b.isVisible("#cfgOther"), true);
    assert.equal(await a.isVisible("#offline"), false);
    assert.equal(await b.isVisible("#offline"), false);
  });
  await step("one config reload per phone after a save, none extra from the saver (issue #8 heap)", async () => {
    const gets = { a: 0, b: 0 };
    const count = (k) => (r) => { if (r.method() === "GET" && r.url().endsWith("/api/config")) gets[k]++; };
    const ca = count("a"), cb = count("b");
    a.on("request", ca); b.on("request", cb);
    await a.fill("#devName", "Pine ridge");
    await a.click("#cfgSave");
    await b.waitForFunction(() => document.getElementById("devName").value === "Pine ridge", null, within(1500));
    await a.waitForTimeout(800);
    a.off("request", ca); b.off("request", cb);
    assert.deepEqual(gets, { a: 1, b: 1 });
    assert.equal(await a.isDisabled("#cfgSave"), true, "saver's form is clean after the reload");
    assert.equal(await a.isVisible("#cfgOther"), false, "own save is not reported as the other phone's");
  });
  await step("name limits count bytes like the board: accented letters take 2", async () => {
    await a.fill("#devName", "é".repeat(13));  // 13 characters, 26 bytes
    await a.click("#cfgSave");
    assert.match(await a.textContent("#fName .err"), /accented letters count as 2/);
    await a.fill("#devName", "é".repeat(12));  // 24 bytes: allowed
    await a.click("#cfgSave");
    await b.waitForFunction(() => document.getElementById("devName").value === "é".repeat(12), null, within(1500));
  });
  await step("an edit in progress is kept when the other phone saves, and flagged", async () => {
    await b.fill("#devName", "Creek bottom");
    await a.fill("#devName", "Oak flat");
    await a.click("#cfgSave");
    await b.waitForFunction(() => /changed Device name/.test(document.getElementById("cfgOtherText").textContent), null, within(1500));
    assert.equal(await b.inputValue("#devName"), "Creek bottom");
    await b.click("#cfgSave");  // b now has the new rev, so its save goes through
    await a.waitForFunction(() => document.getElementById("devName").value === "Creek bottom", null, within(1500));
  });
  await step("a save based on old settings is refused and reloaded, not silently applied", async () => {
    // b keeps the rev it has now; a saves first, then b posts with the old rev.
    const rev = await b.evaluate(() => cfg.rev);
    await a.fill("#devName", "Hardwoods");
    await a.click("#cfgSave");
    await a.waitForFunction(() => document.getElementById("toast").textContent === "Saved");
    const r = await b.evaluate(async (rev) => (await fetch("/api/config", { method: "POST", body: new URLSearchParams({ ap_ssid: "TurkeyCaller", device_name: "Lost update", rev }) })).status, rev);
    assert.equal(r, 409);
    await b.waitForFunction(() => document.getElementById("devName").value === "Hardwoods", null, within(1500));
  });
  await step("inline errors for bad values, nothing sent", async () => {
    await a.fill("#apSsid", "");
    await a.click("#cfgSave");
    assert.match(await a.textContent("#fSsid .err"), /1 to 32/);
    await a.fill("#apSsid", "TurkeyCaller");
  });
  await step("Wi-Fi change: Save and restart, both phones told to rejoin the new name", async () => {
    await a.fill("#apSsid", "GobbleNet");
    await a.fill("#apPass", "longbeard22");
    assert.equal(await a.textContent("#cfgSave"), "Save and restart");
    await a.click("#cfgSave");
    await a.waitForFunction(() => /Rejoin GobbleNet/.test(document.getElementById("offText").textContent), null, within(1500));
    await b.waitForFunction(() => !document.getElementById("offline").hidden && /other phone/.test(document.getElementById("offText").textContent) && /GobbleNet/.test(document.getElementById("offText").textContent), null, within(1500));
  });
  await step("after the restart both phones reconnect with the new settings and no reminder", async () => {
    for (const p of [a, b]) {
      await p.waitForFunction(() => document.getElementById("offline").hidden, null, within(15000));
      await p.waitForFunction(() => document.getElementById("apSsid").value === "GobbleNet", null, within(8000));
      assert.equal(await p.isVisible("#cfgDefPw"), false);
    }
  });
  await step("BOOT reset: phones are told settings were reset, then show the defaults", async () => {
    await fetch(URL + "api/mock/boot-reset", { method: "POST" });
    for (const p of [a, b]) await p.waitForFunction(() => document.getElementById("offTitle").textContent === "Wi-Fi settings reset", null, within(1500));
    for (const p of [a, b]) {
      await p.waitForFunction(() => document.getElementById("apSsid").value === "TurkeyCaller" && document.getElementById("offline").hidden, null, within(20000));
      assert.equal(await p.isVisible("#cfgDefPw"), true);
    }
  });
  await step("volume and Repeat set on one phone show on the other", async () => {
    await a.click('nav [data-go="calls"]');
    await b.click('nav [data-go="calls"]');
    await a.waitForFunction(() => !document.getElementById("volUp").disabled);
    const v = +(await a.inputValue("#vol"));
    await a.click("#volUp");
    await b.waitForFunction((v) => +document.getElementById("vol").value === v + 5, v, within(1500));
    await a.click('label[for="loopSw"]');
    await b.waitForFunction(() => document.getElementById("loopSw").checked, null, within(1500));
  });
  await step("no sideways scrolling on Settings at phone width", async () => {
    await a.click('nav [data-go="settings"]');
    assert.equal(await a.evaluate(() => document.documentElement.scrollWidth), 375);
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
