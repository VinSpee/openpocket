// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the install page (docs/index.html) in a real browser (headless Chromium, Playwright), its Web MIDI
// bridged to the simulated pedal (tests/devsim.c): Connect, then Install, as a user would; screenshots in OUTDIR.
// The site has no catalog at first (one firmware, from its manifest), then a firmware offered by ?app= only, then a
// catalog of two firmwares (release.json, tools/catalog.py): install each, switch, go back, and what is refused.
//   node tests/test_page.mjs OUTDIR
import { spawn, execFileSync } from "node:child_process";
import { mkdtempSync, cpSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join, extname } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const OUT = process.argv[2] || mkdtempSync(join(tmpdir(), "oppage-"));
const SITE = mkdtempSync(join(tmpdir(), "opsite-"));
const FIX = mkdtempSync(join(tmpdir(), "opfix-"));
execFileSync("python3", [join(ROOT, "tests", "fixtures.py"), join(FIX, "firmware")], { stdio: "inherit" });
cpSync(join(ROOT, "docs"), SITE, { recursive: true });
rmSync(join(SITE, "firmware"), { recursive: true, force: true });   // (not what a build left in docs/firmware: its catalog)
cpSync(join(FIX, "firmware"), join(SITE, "firmware"), { recursive: true });

const TYPES = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".fwsc": "application/octet-stream" };
const server = createServer((req, res) => {
  try {
    const p = join(SITE, decodeURIComponent(req.url.split("?")[0]).replace(/\/$/, "/index.html"));
    const body = readFileSync(p);
    res.writeHead(200, { "content-type": TYPES[extname(p)] || "application/octet-stream" });
    res.end(body);
  } catch (_) { res.writeHead(404); res.end(); }
}).listen(0);
const url = `http://127.0.0.1:${server.address().port}/`;

let sim = null;
const simLog = [];
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 820, height: 1100 }, colorScheme: "dark" });
await page.exposeFunction("simSend", (hex) => sim && sim.stdin.write(hex + "\n"));
page.on("console", (m) => { if (m.type() === "error") console.log("page error:", m.text()); });
page.on("pageerror", (e) => console.log("page exception:", e.message));
await page.addInitScript(() => {
  // a Web MIDI access over the simulated pedal; "#port NAME" renames (re-enumerates) its port pair
  const access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
  let port = null;
  window.__simPort = (name) => {
    if (port) { port.input.state = "disconnected"; port.output.state = "disconnected"; }
    access.inputs.clear(); access.outputs.clear(); port = null;
    if (name !== "none") {
      const input = { name, state: "connected", onmidimessage: null, open: async () => {} };
      const output = { name, state: "connected", open: async () => {}, send: (b) => {
        if (output.state === "disconnected") throw new Error("disconnected");
        window.simSend([...b].map((x) => x.toString(16).padStart(2, "0")).join(""));
      } };
      port = { input, output };
      access.inputs.set(name, input); access.outputs.set(name, output);
    }
    if (access.onstatechange) access.onstatechange({});
  };
  window.__simIn = (hex) => {
    if (port && port.input.onmidimessage) port.input.onmidimessage({ data: Uint8Array.from(hex.match(/../g).map((h) => parseInt(h, 16))) });
  };
  navigator.requestMIDIAccess = async () => access;
});
let buf = "";
function startSim(...args) {
sim = spawn(join(ROOT, "build", "devsim"), [join(FIX, "firmware", "flash_old.bin"), ...args], { stdio: ["pipe", "pipe", "inherit"] });
sim.stdout.on("data", (d) => {
  buf += d.toString();
  let i;
  while ((i = buf.indexOf("\n")) >= 0) {
    const ln = buf.slice(0, i); buf = buf.slice(i + 1);
    if (ln.startsWith("#port ")) page.evaluate((n) => window.__simPort(n), ln.slice(6)).catch(() => {});
    else if (ln.startsWith("#log ")) simLog.push(ln.slice(5));
    else if (ln) page.evaluate((h) => window.__simIn(h), ln).catch(() => {});
  }
});
}

// a firmware's card on the page, by the product id a pedal reports, and its one button
const card = (id) => `#fws [data-fw=${id}]`, btn = (id) => `${card(id)} button`;
const OP = btn("openpocket"), ALT = btn("starter");
const shown = () => page.$$eval("#fws [data-fw]", (c) => c.map((e) => e.dataset.fw).join(","));
const step2 = () => simLog.filter((l) => /step2 rc=0/.test(l)).length;
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(64)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
await page.goto(url);
await page.waitForTimeout(300);
startSim();
await page.waitForTimeout(300);
await page.screenshot({ path: join(OUT, "page_1_start.png"), fullPage: true });
await page.click("#connect");
await page.waitForSelector("#devinfo:not([hidden])", { timeout: 15000 });
const fw = await page.textContent("#d-fw");
ok(/OpenPocket v23/.test(fw), `detected: ${fw}`);
ok(await shown() === "openpocket" && /Choose a firmware/i.test(await page.textContent("#installCard h2")), "a site without a catalog: the one firmware of its manifest, in a card");
ok(await page.isEnabled(OP) && (await page.textContent(OP)) === "Install OpenPocket v24" && await page.isVisible(`${card("openpocket")} .pill`),
  "its button names the firmware and the version, and the card says the pedal runs this firmware");
await page.screenshot({ path: join(OUT, "page_2_found.png"), fullPage: true });
await page.click(OP);
await page.waitForFunction(() => /PocketX_924/.test(document.getElementById("msg").textContent) ||
  document.getElementById("msg").classList.contains("err"), null, { timeout: 240000 });
const msg = await page.textContent("#msg");
ok(/PocketX_924/.test(msg), `result: ${msg}`);
ok(/OpenPocket v24/.test(msg) && await page.isVisible("#next") && /WHATS-NEW-v24\.md$/.test(await page.getAttribute("#notes", "href")), "the result names the version and links the editor and the release notes");
ok(await page.$$eval("#steps span", (s) => s.length > 0 && s.every((e) => e.className === "done")), "every stage shows done, none still lit");
await page.waitForTimeout(1500);
ok(/OpenPocket v24/.test(await page.textContent("#d-fw")) && (await page.textContent(OP)) === "Install again: OpenPocket v24", "the page now sees OpenPocket v24, and the button says Install again");
await page.screenshot({ path: join(OUT, "page_3_done.png"), fullPage: true });
ok(simLog.some((l) => /step2 rc=0/.test(l)), "the simulated pedal wrote the new app");

// rescue mode: armed with no pedal; the install starts by itself when the pedal appears (issue #1)
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
await page.reload();
await page.waitForTimeout(300);
ok(!(await page.isVisible("#rescueOn")), "rescue mode sits in the advanced section, folded");
await page.click("details summary[data-t=rescue]");
await page.check("#rescueOn");
await page.waitForFunction(() => /rescue mode armed/.test(document.getElementById("log").textContent), null, { timeout: 15000 });
await page.click("#connect");
await page.waitForTimeout(300);
ok(!/PocketX_9/.test(await page.textContent("#msg")), "rescue: armed, nothing happens without a pedal");
startSim();
await page.waitForFunction(() => /PocketX_924/.test(document.getElementById("msg").textContent), null, { timeout: 240000 });
ok(/PocketX_924/.test(await page.textContent("#msg")), "rescue: the pedal appears, the install runs by itself");
ok(!(await page.isChecked("#rescueOn")), "rescue: the mode turns itself off once done");
await page.screenshot({ path: join(OUT, "page_4_rescue.png"), fullPage: true });

// V21 on its own is not proof that the factory bank is readable.
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
await page.reload();
startSim("--stock", "BlackBox_021");
await page.click("#connect");
await page.waitForSelector("#verify-factory:enabled", { timeout: 15000 });
ok(await page.isVisible("#v21-factory"), "BlackBox V21: the factory sound controls are visible");
await page.click("#verify-factory");
await page.waitForSelector("#msg.err", { timeout: 15000 });
ok(/sound bank/.test(await page.textContent("#msg")), "BlackBox V21: no bank reply means verification failed");
await page.click("#factory-uboot");
await page.waitForFunction(() => /USB recovery requested/.test(document.getElementById("msg").textContent));
ok(simLog.includes("uboot") && !simLog.some((l) => /step[12]/.test(l)), "BlackBox V21: recovery asks for UBOOT without running an OTA");

// an official firmware newer than any the page knows: a warning and a checkbox. Install stays disabled until it is
// ticked; once ticked the install completes on the simulated pedal
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
await page.reload();
startSim("--stock", "PocketX_017");
await page.click("#connect");
await page.waitForSelector("#newerconfirm:not([hidden])", { timeout: 15000 });
ok(/official firmware 017.*precaution/s.test(await page.textContent("#newerwarn")), "official PocketX_017 (unknown): the warning names the firmware and calls the check a precaution");
ok(await page.isDisabled(OP) && !simLog.some((l) => /step[12]/.test(l)), "official PocketX_017: Install stays disabled until confirmed, nothing sent to the pedal");
await page.screenshot({ path: join(OUT, "page_4b_untested.png"), fullPage: true });
await page.check("#newerok");
await page.waitForSelector(`${OP}:enabled`, { timeout: 15000 });
await page.click(OP);
await page.waitForFunction(() => /PocketX_924/.test(document.getElementById("msg").textContent), null, { timeout: 240000 });
ok(simLog.some((l) => /step2 rc=0/.test(l)), "official PocketX_017, confirmed: the install completes on the simulated pedal");

// a package that is not the manifest's (one byte changed on the site): refused by its SHA-256, before the pedal is asked
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
{
  const f = join(SITE, "firmware", "PocketX_924.fwsc"), good = readFileSync(f), bad = Buffer.from(good);
  bad[bad.length >> 1] ^= 1;
  writeFileSync(f, bad);
  await page.reload();
  startSim();
  await page.click("#connect");
  await page.waitForSelector(`${OP}:enabled`, { timeout: 15000 });
  await page.click(OP);
  await page.waitForSelector("#msg.err", { timeout: 15000 });
  ok(/SHA-256 mismatch/.test(await page.textContent("#msg")), "a package that differs from the manifest: refused (SHA-256)");
  ok(!simLog.some((l) => /step[12]/.test(l)), "the changed package: no update step ran on the pedal");
  writeFileSync(f, good);
}

// the USB rescue for a pedal the page cannot reach: a folded guide in the rescue section, Mac and Windows steps,
// read-only first, the write as one more step, a link to the full guide; it fits a phone's width
{
  ok(/read-only/.test(await page.textContent("[data-t=rescueusb]")) && /How to run the rescue on your computer/.test(await page.textContent("[data-t=rescueon]")),
    "rescue section: the USB rescue is read-only first, and rescue mode points to the guide");
  await page.click("details summary[data-t=rescue]");
  ok(!(await page.isVisible("#rescue-mac")), "rescue guide: folded until opened");
  await page.click("#rescueguide summary");
  const steps = (id) => page.$$eval(`#${id} + ol li, #${id} + p + ol li`, (li) => li.map((e) => e.textContent));
  const mac = await steps("rescue-mac"), win = await steps("rescue-windows");
  ok(mac.some((s) => /brew install libusb/.test(s)) && mac.some((s) => /sudo python3 tools\/uboot_rescue\.py --package BlackBox\.fwsc/.test(s)) &&
     mac.some((s) => /--write --package BlackBox\.fwsc/.test(s)), "rescue guide, Mac: libusb once, the read-only check, then the write");
  ok(win.some((s) => /python\.org/.test(s)) && win.some((s) => /Run as administrator/.test(s)) && win.some((s) => /^Unplug.*RESCUE\.bat --package BlackBox\.fwsc/.test(s)) &&
     win.some((s) => /RESCUE\.bat --write --package BlackBox\.fwsc/.test(s)) && /not been tested on a real pedal/.test(await page.textContent("#rescueguide .warnbox")),
    "rescue guide, Windows: Python once, an administrator Command Prompt, the check, then the write; said to be untested");
  ok(await page.getAttribute("#rescueguide a[href^='RESCUE.md#quick-start']", "href") !== null, "rescue guide: links the full guide in RESCUE.md");
  await page.setViewportSize({ width: 360, height: 800 });
  ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), "rescue guide: no sideways scrolling at a phone's width");
  await page.screenshot({ path: join(OUT, "page_4c_rescue_guide.png"), fullPage: true });
  await page.setViewportSize({ width: 820, height: 1100 });
}

// a test channel: only through ?channel=NAME, from its own folder, with its banner; the normal page has none
cpSync(join(FIX, "firmware"), join(SITE, "firmware", "midi-test"), { recursive: true });
{
  const ch = await browser.newPage({ viewport: { width: 820, height: 900 } });
  await ch.goto(url + "?channel=midi-test");
  await ch.waitForSelector(card("openpocket"), { timeout: 5000 });
  ok(await ch.isVisible("#testchan"), "test channel: the banner says it is a test build");
  ok((await ch.textContent(`${card("openpocket")} small`)) === "v24 (midi-test)", "test channel: the packages come from firmware/midi-test/");
  const asked = [];
  ch.on("request", (r) => asked.push(new URL(r.url()).pathname));
  await ch.goto(url + "?channel=../../etc");
  await ch.waitForSelector("#nofw:not([hidden])", { timeout: 5000 });
  ok(asked.includes("/firmware/etc/manifest.json") && !(await ch.$("#fws [data-fw]")), "test channel: a path in the name is cleaned (firmware/etc/ is asked for, nothing outside firmware/); no firmware is offered");
  await ch.screenshot({ path: join(OUT, "page_5_test_channel.png"), fullPage: true });
  ok(!(await page.isVisible("#testchan")), "the normal page shows no test banner");
  ok(await shown() === "openpocket", "the normal page offers no firmware that is not on its list");
  await ch.close();
}

// a firmware that is not on the site's list (an app on the base firmware): only through ?app=NAME, in a card of its
// own after the listed one; the page says which product the pedal runs, and the first card is the way back
cpSync(join(FIX, "firmware", "alt"), join(SITE, "firmware", "apps", "starter"), { recursive: true });
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
await page.goto(url + "?app=starter");
await page.waitForSelector(card("starter"), { timeout: 5000 });
ok(await shown() === "openpocket,starter" && /STARTER\s+v01/.test(await page.textContent(`${card("starter")} h3`)) && /by this link only/.test(await page.textContent(card("starter"))),
  "?app=starter: its own card names STARTER v01 and says it is offered by this link only");
ok(!/by this link only/.test(await page.textContent(card("openpocket"))), "the listed firmware keeps its card, with no such note");
ok(!(await page.isEnabled(ALT)) && !(await page.isEnabled(OP)), "nothing can be installed before a pedal is connected");
startSim();
await page.waitForTimeout(300);
await page.click("#connect");
await page.waitForSelector("#devinfo:not([hidden])", { timeout: 15000 });
ok(/OpenPocket v23/.test(await page.textContent("#d-fw")) && (await page.textContent(OP)) === "Install OpenPocket v24", "a pedal on OpenPocket: named so, its card's button says Install");
ok((await page.textContent(ALT)) === "Switch to STARTER v01" && !(await page.isVisible(`${card("starter")} .pill`)), "the other firmware's button says Switch to, with its name");
await page.screenshot({ path: join(OUT, "page_6_alt_offered.png"), fullPage: true });
await page.click(ALT);
await page.waitForFunction(() => /PocketX_901/.test(document.getElementById("msg").textContent) || document.getElementById("msg").classList.contains("err"), null, { timeout: 240000 });
ok((await page.textContent("#msg")) === "Done: the pedal now runs STARTER v01 (PocketX_901)", `result: ${await page.textContent("#msg")}`);
ok(!(await page.isVisible("#next")), "no editor link and no release notes for a firmware that is not OpenPocket");
await page.waitForFunction(() => /STARTER/.test(document.getElementById("d-fw").textContent), null, { timeout: 15000 });
ok((await page.textContent("#d-fw")) === "STARTER v01 (PocketX_901)", `the page now sees: ${await page.textContent("#d-fw")}`);
ok((await page.textContent(OP)) === "Switch to OpenPocket v24" && (await page.textContent(ALT)) === "Install again: STARTER v01" &&
  await page.isVisible(`${card("starter")} .pill`) && !(await page.isVisible(`${card("openpocket")} .pill`)),
  "the first card's button is now Switch to OpenPocket; STARTER's card says the pedal runs it");
ok(await page.isEnabled("#uboot"), "the UBOOT soft key is the base firmware's: offered for every product on it");
await page.screenshot({ path: join(OUT, "page_7_alt_running.png"), fullPage: true });
{
  const pub = await browser.newPage({ viewport: { width: 820, height: 900 } });   // (a second page: no MIDI access of its own)
  await pub.goto(url);
  await pub.waitForSelector(card("openpocket"), { timeout: 5000 });
  ok(!(await pub.$(card("starter"))), "without the link the same site offers the listed firmware only");
  await pub.close();
}
await page.click(OP);
await page.waitForFunction(() => /PocketX_924/.test(document.getElementById("msg").textContent) || document.getElementById("msg").classList.contains("err"), null, { timeout: 240000 });
ok(/OpenPocket v24 \(PocketX_924\)/.test(await page.textContent("#msg")), `the way back: ${await page.textContent("#msg")}`);
await page.waitForFunction(() => /OpenPocket v24/.test(document.getElementById("d-fw").textContent), null, { timeout: 15000 });
ok((await page.textContent(OP)) === "Install again: OpenPocket v24" && (await page.textContent(ALT)) === "Switch to STARTER v01", "the pedal is on OpenPocket again, and the page says so");
ok(step2() === 2, "the simulated pedal wrote both firmwares");
await page.screenshot({ path: join(OUT, "page_8_back_on_openpocket.png"), fullPage: true });
{
  const no = await browser.newPage({ viewport: { width: 820, height: 900 } });
  await no.goto(url + "?app=nosuch");
  await no.waitForFunction(() => document.getElementById("msg").textContent !== "", null, { timeout: 5000 });
  ok(/No firmware by this name on this site: nosuch$/.test(await no.textContent("#msg")) && !(await no.$(card("nosuch"))), "?app= with a name the site does not have: said so, nothing to install");
  await no.goto(url + "?app=../../etc");
  await no.waitForFunction(() => document.getElementById("msg").textContent !== "", null, { timeout: 5000 });
  ok(/: etc$/.test(await no.textContent("#msg")), "a path in the name is cleaned (firmware/apps/etc/ is asked for, nothing outside it)");
  await no.close();
}

// the chooser: a site with a catalog (release.json) shows one card per firmware of it, with no link needed. Install
// each, switch from one to the other and back; a firmware is refused before anything is sent when the pedal runs an
// official firmware the page has not been checked with, and when its package is not the catalog's
{
  const source = join(FIX, "catalog.json");
  writeFileSync(source, JSON.stringify({ firmwares: [
    { id: "openpocket", name: "OpenPocket", what: "The first firmware of the test site." },
    { id: "starter", what: "The second firmware of the test site.", note: "Not yet run on a real pedal." }] }));
  execFileSync("python3", [join(ROOT, "tools", "catalog.py"), "--source", source, "--site", join(SITE, "firmware"), "--apps", join(SITE, "firmware", "apps")], { stdio: "inherit" });
}
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
await page.goto(url);
await page.waitForSelector(card("starter"), { timeout: 5000 });
ok(await shown() === "openpocket,starter", "a site with a catalog: one card per firmware, in the catalog's order, with no link");
ok(/The first firmware of the test site\./.test(await page.textContent(card("openpocket"))) && /The second firmware of the test site\./.test(await page.textContent(card("starter"))) &&
  /STARTER\s+v01/.test(await page.textContent(`${card("starter")} h3`)) && /OpenPocket\s+v24/.test(await page.textContent(`${card("openpocket")} h3`)),
  "each card has the firmware's name, its version and the catalog's line on what it is");
ok(!/by this link only/.test(await page.textContent("#fws")), "a firmware of the catalog is not marked as offered by a link");
ok((await page.textContent(`${card("starter")} .warnbox`)) === "Not yet run on a real pedal." && !(await page.$(`${card("openpocket")} .warnbox`)), "a firmware's warning in the catalog is shown in its card, and only there");
ok(!(await page.isEnabled(OP)) && !(await page.isEnabled(ALT)), "chooser: nothing can be installed before a pedal is connected");
startSim();
await page.waitForTimeout(300);
await page.click("#connect");
await page.waitForSelector(`${ALT}:enabled`, { timeout: 15000 });
await page.screenshot({ path: join(OUT, "page_9_chooser.png"), fullPage: true });
await page.click(ALT);
await page.waitForFunction(() => /PocketX_901/.test(document.getElementById("msg").textContent) || document.getElementById("msg").classList.contains("err"), null, { timeout: 240000 });
ok((await page.textContent("#msg")) === "Done: the pedal now runs STARTER v01 (PocketX_901)", `chooser, the second firmware installed: ${await page.textContent("#msg")}`);
await page.waitForSelector(`${OP}:enabled`, { timeout: 15000 });
ok((await page.textContent(OP)) === "Switch to OpenPocket v24" && await page.isVisible(`${card("starter")} .pill`), "chooser: the first firmware's button now says Switch to, and names it");
await page.screenshot({ path: join(OUT, "page_10_chooser_switched.png"), fullPage: true });
await page.click(OP);
await page.waitForFunction(() => /PocketX_924/.test(document.getElementById("msg").textContent) || document.getElementById("msg").classList.contains("err"), null, { timeout: 240000 });
ok((await page.textContent("#msg")) === "Done: the pedal now runs OpenPocket v24 (PocketX_924)" && await page.isVisible("#next"), `chooser, switched to the first firmware: ${await page.textContent("#msg")}`);
await page.waitForSelector(`${ALT}:enabled`, { timeout: 15000 });
ok((await page.textContent(ALT)) === "Switch to STARTER v01" && (await page.textContent(OP)) === "Install again: OpenPocket v24", "chooser: the second firmware's button says Switch to");
await page.click(ALT);
await page.waitForFunction(() => /PocketX_901/.test(document.getElementById("msg").textContent) || document.getElementById("msg").classList.contains("err"), null, { timeout: 240000 });
await page.waitForFunction(() => /STARTER/.test(document.getElementById("d-fw").textContent), null, { timeout: 15000 });
ok(/STARTER v01 \(PocketX_901\)/.test(await page.textContent("#msg")) && step2() === 3, "chooser: and back to the second firmware; the simulated pedal wrote each of the three installs");
{
  const also = await browser.newPage({ viewport: { width: 820, height: 900 } });
  await also.goto(url + "?app=starter");
  await also.waitForSelector(card("starter"), { timeout: 5000 });
  ok(await also.$$eval("#fws [data-fw]", (c) => c.length) === 2 && !/by this link only/.test(await also.textContent("#fws")), "?app= with a firmware of the catalog: no second card for it");
  await also.setViewportSize({ width: 360, height: 800 });
  ok(await also.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), "chooser: no sideways scrolling at a phone's width");
  await also.screenshot({ path: join(OUT, "page_11_chooser_phone.png"), fullPage: true });
  await also.close();
}
// the refusals: an official firmware newer than the page knows holds every card's button until it is confirmed
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
await page.reload();
startSim("--stock", "PocketX_017");
await page.click("#connect");
await page.waitForSelector("#newerconfirm:not([hidden])", { timeout: 15000 });
ok(await page.isDisabled(OP) && await page.isDisabled(ALT) && !simLog.some((l) => /step[12]/.test(l)),
  "chooser, official PocketX_017 (unknown): neither firmware can be installed until confirmed, nothing sent to the pedal");
ok((await page.textContent(ALT)) === "Install STARTER v01" && !(await page.isVisible(`${card("starter")} .pill`)) && !(await page.isVisible(`${card("openpocket")} .pill`)),
  "chooser, a pedal on the official firmware: the buttons say Install, no card says the pedal runs it");
await page.screenshot({ path: join(OUT, "page_12_chooser_refused.png"), fullPage: true });
// a package that is not the catalog's (one byte changed on the site): refused by its SHA-256, before the pedal is asked
sim.stdin.end(); sim = null; simLog.length = 0; buf = "";
{
  const f = join(SITE, "firmware", "apps", "starter", "starter-PocketX_901.fwsc"), bad = Buffer.from(readFileSync(f));
  bad[bad.length >> 1] ^= 1;
  writeFileSync(f, bad);
  await page.reload();
  startSim();
  await page.click("#connect");
  await page.waitForSelector(`${ALT}:enabled`, { timeout: 15000 });
  await page.click(ALT);
  await page.waitForSelector("#msg.err", { timeout: 15000 });
  ok(/SHA-256 mismatch/.test(await page.textContent("#msg")) && !simLog.some((l) => /step[12]/.test(l)), "chooser, a package that differs from the catalog: refused (SHA-256), no update step ran on the pedal");
  // a catalog that cannot be read: the page falls back to the folder's manifest, one firmware
  rmSync(join(SITE, "firmware", "release.json"));
  writeFileSync(join(SITE, "firmware", "release.json"), "{ not json");
  await page.reload();
  await page.waitForSelector(card("openpocket"), { timeout: 5000 });
  ok(await shown() === "openpocket", "a catalog that cannot be read: the firmware of the folder's manifest is offered, nothing else");
}
await browser.close();
if (sim) sim.stdin.end();
server.close();
console.log(failed ? `${failed} FAILED` : "page test passed", "| screenshots:", OUT);
process.exit(failed ? 1 : 0);
