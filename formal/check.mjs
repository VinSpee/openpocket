// SPDX-License-Identifier: GPL-3.0-only
// node formal/check.mjs: model-check every specification in this folder with everystate, and check the order the
// firmware's own flash code runs in against the flash model (formal/README.md).
// A configuration that models a known finding or a mutation is expected to fail, on the property named here: the
// run is wrong when a configuration passes that should fail, as much as the other way round.
//   node formal/check.mjs [--no-traces] [NAME...]
//   --no-traces  leave out the recorded runs (they need a C compiler)
//   NAME         only the configurations and traces whose name contains it
// The checker: formal/node_modules (npm ci --prefix formal), or the binary EVERYSTATE names.
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, "..");

// [specification, configuration, expected status, the property that fails, why it is expected to]
const MODELS = [
  ["FlashHold", "FlashHold", "invariant", "XipExclusion", "finding F1"],
  ["FlashHold", "FlashHoldFixed", "ok"],
  ["FlashHold", "FlashHoldOneShot", "ok"],
  ["FlashHold", "FlashHoldNoCollect", "invariant", "XipExclusion", "mutation"],
  ["EditorWatch", "EditorWatch", "invariant", "InSync", "finding F2"],
  ["EditorWatch", "EditorWatchFixed", "ok"],
  ["PresetStore", "PresetStore", "ok"],
  ["PresetStore", "PresetStoreStrict", "invariant", "OnlyConfirmedOrInProgress", "note N1"],
  ["PresetStore", "PresetStoreOverwrite", "invariant", "NoLostSave", "mutation"],
  ["KnobHandoff", "KnobHandoff", "ok"],
  ["KnobHandoff", "KnobHandoffNoWait", "invariant", "NoTornHandoff", "mutation"],
  ["LayerSplit", "LayerSplit", "ok"],
  ["LayerSplit", "LayerSplitNoCheck", "invariant", "RingExclusive", "mutation"],
];

// The runs conformance/flash_trace.c records from the firmware's own code, against FlashHold.tla:
// [trace, configuration, expected status, why a mismatch is expected]
const HELD = ["nm_commit", "nm_erase", "ir_commit", "ir_erase"];
const TRACES = [
  ["st_save", "FlashHold", "conforms"],
  ["st_save", "FlashHoldFixed", "conforms"],
  ...HELD.flatMap((t) => [[t, "FlashHold", "conforms"], [t, "FlashHoldFixed", "mismatch", "finding F1"]]),
];

const args = process.argv.slice(2);
const noTraces = args.includes("--no-traces");
const only = args.filter((a) => !a.startsWith("--"));
const wanted = (name) => !only.length || only.some((o) => name.includes(o));

function checker() {
  if (process.env.EVERYSTATE) return process.env.EVERYSTATE;
  const local = join(HERE, "node_modules", ".bin", "everystate");
  if (existsSync(local)) return local;
  console.error("formal/check.mjs: the checker is not installed. Run `npm ci --prefix formal`, or set EVERYSTATE to an everystate binary.");
  process.exit(2);
}
const ES = checker();

// one JSON object on the last line of stdout
function everystate(argv) {
  const r = spawnSync(ES, argv, { cwd: HERE, encoding: "utf8", maxBuffer: 64 << 20 });
  if (r.error) return { status: `cannot run: ${r.error.message}` };
  try { return JSON.parse(r.stdout.trim().split("\n").pop()); }
  catch (_) { return { status: `no result (exit ${r.status}): ${(r.stderr || r.stdout).trim().split("\n").pop()}` }; }
}

let failed = 0;
function report(name, got, want, detail, why) {
  const ok = got === want;
  if (!ok) failed++;
  console.log(`${ok ? "ok  " : "FAIL"}  ${name.padEnd(34)} ${got}${detail ? `, ${detail}` : ""}${ok ? (why ? `  (expected: ${why})` : "") : `  -- expected: ${want}`}`);
}

for (const [spec, cfg, status, prop, why] of MODELS) {
  if (!wanted(cfg)) continue;
  const r = everystate(["-json", "-config", `${cfg}.cfg`, `${spec}.tla`]);
  const got = r.name ? `${r.status} ${r.name}` : r.status;
  report(`${cfg}.cfg`, got, prop ? `${status} ${prop}` : status, r.status === "ok" ? `${r.distinct} states` : "", why);
}

if (!noTraces && TRACES.some(([t, cfg]) => wanted(`${t}:${cfg}`))) {
  const dir = mkdtempSync(join(tmpdir(), "openpocket-formal-"));
  try {
    const exe = join(dir, "flash_trace");
    const cc = spawnSync(process.env.CC || "cc", ["-O1", "-Iengine", "formal/conformance/flash_trace.c", "engine/px_dsp.c",
      "firmware/px_math.c", "-lm", "-o", exe], { cwd: ROOT, encoding: "utf8" });
    const run = cc.status === 0 ? spawnSync(exe, [dir], { encoding: "utf8" }) : null;
    if (!run || run.status !== 0) {
      failed++;
      console.log(`FAIL  conformance/flash_trace.c: ${cc.status === 0 ? `it returned ${run.status}: ${run.stderr.trim()}` : `it does not build: ${(cc.error ? cc.error.message : cc.stderr).trim()}`}`);
    } else for (const [trace, cfg, status, why] of TRACES) {
      if (!wanted(`${trace}:${cfg}`)) continue;
      const r = everystate(["conform", "-json", "-trace", join(dir, `${trace}.jsonl`), "-config", `${cfg}.cfg`, "FlashHold.tla"]);
      report(`${trace} : ${cfg}.cfg`, r.status, status, r.status === "mismatch" ? `line ${r.line} of ${r.observations}` : "", why);
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
}

console.log(failed ? `${failed} not as expected` : "all as expected");
process.exit(failed ? 1 : 0);
