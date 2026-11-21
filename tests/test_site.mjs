// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the site container (site/Dockerfile, site/README.md), built and run with Docker on this computer:
//   1. the image as it will be deployed, with the release that site/RELEASE pins: the pages, their types, the pinned bytes,
//      the rescue kit;
//   2. the build's release step against a stand-in release (the fixture packages, served here): every file is the
//      pinned one, and a build with a wrong pin stops;
//   3. the installer and editor page tests (tests/test_page.mjs, tests/test_editor_page.mjs) against the container.
// It needs Docker, Playwright (NODE_PATH) and the simulators in build/: `sh .claude/skills/verify/scripts/pages.sh site`.
//   node tests/test_site.mjs [OUT_DIR]     (the page tests' screenshots in OUT_DIR, default build/site)
import { execFile, execFileSync, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const OUT = process.argv[2] || join(ROOT, "build", "site");
const TMP = mkdtempSync(join(tmpdir(), "opsitetest-"));
const IMAGE = `openpocket-site-test-${process.pid}`;   // its own images and containers: another run may share this Docker
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(100)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const docker = (...args) => execFileSync("docker", args, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();
const containers = [], images = [];

// not a blocking call: the stand-in release below is served by this process while the build downloads from it
const build = (tag, context, ...args) => new Promise((done) => {
  execFile("docker", ["build", "--progress=plain", "-f", join(context, "site", "Dockerfile"), "-t", tag, ...args, context], (error, _, log) => {
    if (!error) images.push(tag);
    done({ status: error ? 1 : 0, log, end: log.trim().split("\n").slice(-12).join("\n") });
  });
});
async function run(image, ...args) {
  const id = docker("run", "-d", "--rm", "-p", "127.0.0.1::80", ...args, image);
  containers.push(id);
  const url = `http://127.0.0.1:${docker("port", id, "80").split("\n")[0].split(":").pop()}/`;
  for (let i = 0; i < 100; i++) {
    try { await fetch(url + "RELEASE"); return url; } catch (_) { await new Promise((r) => setTimeout(r, 100)); }
  }
  throw new Error(`the container of ${image} does not answer at ${url}`);
}
// a pin file's lines: [SHA-256, path on the site, asset name]
const pinned = (text) => text.split("\n").filter((l) => l && !l.startsWith("#")).map((l) => l.split(/\s+/));
async function servesPin(url, tag, lines, what) {
  ok((await (await fetch(url + "RELEASE")).text()).trim() === tag, `${what}: /RELEASE says ${tag}`);
  for (const [sum, path] of lines) {
    const r = await fetch(url + path);
    ok(r.status === 200 && sha256(Buffer.from(await r.arrayBuffer())) === sum, `${what}: /${path} is the pinned file`);
  }
}

try {
  // 1. the image Coolify will build: this checkout's pages and the release pinned in site/
  const tag = readFileSync(join(ROOT, "site", "RELEASE"), "utf8").trim();
  const lines = pinned(readFileSync(join(ROOT, "site", "release.sha256"), "utf8"));
  const deployed = await build(`${IMAGE}:deployed`, ROOT);
  ok(deployed.status === 0, `site/Dockerfile builds with the pinned release (${tag}, ${lines.length} files)${deployed.status ? ":\n" + deployed.end : ""}`);
  if (deployed.status) throw new Error("no image to test");
  const live = await run(`${IMAGE}:deployed`);
  for (const [path, type] of [["", "text/html"], ["editor.html", "text/html"], ["pxota.js", "text/javascript"], ["editor.css", "text/css"], ["RESCUE.md", "text/markdown"]]) {
    const r = await fetch(live + path);
    ok(r.status === 200 && r.headers.get("content-type") === `${type}; charset=utf-8` && (await r.text()) === readFileSync(join(ROOT, "docs", path || "index.html"), "utf8"),
      `the site serves /${path} as ${type}, and it is this checkout's file`);
  }
  ok((await fetch(live)).headers.get("cache-control") === "no-cache", "a browser asks again before it reuses a file (Cache-Control: no-cache)");
  await servesPin(live, tag, lines, "the deployed image");
  ok((await (await fetch(live + "release.sha256")).text()) === readFileSync(join(ROOT, "site", "release.sha256"), "utf8"), "/release.sha256 is the pin of this checkout");
  const [kitSum, kitFile] = readFileSync(join(ROOT, "rescue", "kit.sha256"), "utf8").trim().split(/\s+/);
  const kitManifest = await (await fetch(live + "rescue/manifest.json")).json();
  const kitZip = await fetch(live + "rescue/" + kitFile);
  ok(kitManifest.file === kitFile && kitManifest.sha256 === kitSum && kitZip.status === 200 && sha256(Buffer.from(await kitZip.arrayBuffer())) === kitSum,
    `the site serves the rescue kit its build made, ${kitFile}, and it is the pinned kit (rescue/kit.sha256)`);
  if (tag === "none") ok((await fetch(live + "firmware/manifest.json")).status === 404, "no release is pinned: the site serves no package, also none a local build left in docs/firmware");

  // 2. the release step, against a stand-in release: the fixture packages as release assets on a server here
  const FIX = join(TMP, "fixtures");
  execFileSync("python3", [join(ROOT, "tests", "fixtures.py"), FIX], { stdio: ["ignore", "ignore", "inherit"] });
  const STAND_IN = "v0.0.0-test";
  const assets = { "manifest.json": "manifest.json", "PocketX_924.fwsc": "PocketX_924.fwsc", "BlackBox_924.fwsc": "BlackBox_924.fwsc", "starter-manifest.json": "alt/manifest.json" };
  const release = createServer((req, res) => {
    const file = req.url.startsWith(`/${STAND_IN}/`) && assets[req.url.slice(STAND_IN.length + 2)];
    if (!file) { res.writeHead(404); res.end(); return; }
    res.writeHead(200); res.end(readFileSync(join(FIX, file)));
  }).listen(0);   // on every interface: the build's container reaches it through the Docker host's gateway
  const from = ["--add-host", "host.docker.internal:host-gateway", "--build-arg", `RELEASE_URL=http://host.docker.internal:${release.address().port}`];
  const sum = (asset) => sha256(readFileSync(join(FIX, assets[asset])));
  const good = [[sum("manifest.json"), "firmware/manifest.json", "manifest.json"], [sum("PocketX_924.fwsc"), "firmware/PocketX_924.fwsc", "PocketX_924.fwsc"],
    [sum("BlackBox_924.fwsc"), "firmware/BlackBox_924.fwsc", "BlackBox_924.fwsc"], [sum("starter-manifest.json"), "firmware/apps/starter/manifest.json", "starter-manifest.json"]];
  const CONTEXT = join(TMP, "context");
  for (const part of ["docs", "site", "rescue", "tools/uboot_rescue.py", "tools/pocketx_fw.py", "RESCUE.bat", "LICENSE"])
    cpSync(join(ROOT, part), join(CONTEXT, part), { recursive: true });
  const withPin = (name, pinTag, pinLines) => {
    writeFileSync(join(CONTEXT, "site", "RELEASE"), pinTag + "\n");
    writeFileSync(join(CONTEXT, "site", "release.sha256"), "# a comment\n\n" + pinLines.map((l) => l.join("  ")).join("\n"));   // no newline at the end
    return build(`${IMAGE}:${name}`, CONTEXT, ...from);
  };
  const standIn = await withPin("release", STAND_IN, good);
  ok(standIn.status === 0, `a build with a pinned release downloads its ${good.length} assets${standIn.status ? ":\n" + standIn.end : ""}`);
  if (standIn.status === 0) await servesPin(await run(`${IMAGE}:release`), STAND_IN, good, "the image of the stand-in release");
  const wrong = (lines, i, change) => lines.map((l, k) => (k === i ? change(l) : l));
  for (const [what, pinTag, pinLines, says] of [
    ["an asset that is not the pinned bytes", STAND_IN, wrong(good, 1, ([s, p, a]) => [s.replace(/^./, (c) => (c === "0" ? "1" : "0")), p, a]), /PocketX_924\.fwsc of v0\.0\.0-test is not the pinned file/],
    ["an asset the release does not have", STAND_IN, wrong(good, 2, ([s, p]) => [s, p, "nosuch.fwsc"]), /could not download nosuch\.fwsc/],
    ["a path that is a page of the site", STAND_IN, wrong(good, 0, ([s, , a]) => [s, "index.html", a]), /index\.html is already a file of the site/],
    ["a line without an asset name", STAND_IN, wrong(good, 3, ([s, p]) => [s, p]), /needs a SHA-256, a path and an asset name/],
    ["files listed while RELEASE says none", "none", good, /RELEASE says none, but .* lists firmware\/manifest\.json/],
    ["a release with no file listed", STAND_IN, [], /lists no file/]]) {
    const r = await withPin("refused", pinTag, pinLines);
    ok(r.status !== 0 && says.test(r.log), `the build stops on ${what}${r.status === 0 ? ": it built" : says.test(r.log) ? "" : ":\n" + r.end}`);
  }
  release.close();

  // 3. the page tests against the deployed image; its firmware/ is a folder here, which the installer test fills and changes
  const FW = join(TMP, "firmware");
  mkdirSync(FW);
  const site = await run(`${IMAGE}:deployed`, "-v", `${FW}:/srv/firmware`);
  for (const [test, dir, env] of [["test_page.mjs", "page", { SITE_FIRMWARE: FW }], ["test_editor_page.mjs", "editor", {}]]) {
    mkdirSync(join(OUT, dir), { recursive: true });
    const r = spawnSync(process.execPath, [join(ROOT, "tests", test), join(OUT, dir)], { stdio: "inherit", env: { ...process.env, SITE_URL: site, ...env } });
    ok(r.status === 0, `tests/${test} passes against the container`);
  }
} finally {
  for (const id of containers) spawnSync("docker", ["rm", "-f", id]);
  for (const tag of images) spawnSync("docker", ["rmi", "-f", tag]);
  rmSync(TMP, { recursive: true, force: true });
}
console.log(failed ? `${failed} FAILED` : "site test passed", "| screenshots:", OUT);
process.exit(failed ? 1 : 0);
