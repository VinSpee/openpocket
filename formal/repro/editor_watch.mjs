// SPDX-License-Identifier: GPL-3.0-only
// node formal/repro/editor_watch.mjs EDSIM: finding F2 (formal/FINDINGS.md) on the firmware's own editor code.
// EDSIM is the simulated pedal of the editor tests (tests/edsim.c, which runs firmware/px_editor.h on a PC):
//   cc -O1 -o build/edsim tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
// Two runs. In each a setting is changed on the pedal twice, the second time within 40 ms of the first change's push.
// In the first run the editor's PING arrives in those 40 ms; in the second no request does.
// Since the fix of F2 both runs end the same; tests/test_editor.mjs holds the case as a test.
import { spawn } from "node:child_process";

const edsim = process.argv[2];
if (!edsim) { console.error("usage: node formal/repro/editor_watch.mjs EDSIM"); process.exit(2); }

const STATE = 5, WATCH = 17, PING = 18, BLOCK = 5, KNOB = 1;
const NAMES = { [STATE]: "STATE", [PING]: "PING" };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// STATE's arguments: slot, flags, focus, then px_preset_t (name 14, on 12, type 12, p[block][knob])
const setting = (frame) => frame.args[3 + 14 + 12 + 12 + BLOCK * 5 + KNOB];

async function run(withPing) {
  const sim = spawn(edsim, [], { stdio: ["pipe", "pipe", "inherit"] });
  const frames = [];
  let waiters = [], text = "";
  sim.stdout.on("data", (d) => {
    text += d;
    for (let i; (i = text.indexOf("\n")) >= 0; text = text.slice(i + 1)) {
      const line = text.slice(0, i);
      if (line.startsWith("#")) continue;
      const b = line.match(/../g).map((x) => parseInt(x, 16));
      const frame = { cmd: b[4], args: b.slice(5, -1) };
      frames.push(frame);
      waiters = waiters.filter((w) => !(w.cmd === frame.cmd && (w.resolve(frame), true)));
    }
  });
  const reply = (cmd) => new Promise((resolve) => waiters.push({ cmd, resolve }));
  const send = (cmd, ...args) => sim.stdin.write([0xF0, 0x7D, 0x4F, 0x50, cmd, ...args, 0xF7].map((x) => x.toString(16).padStart(2, "0")).join("") + "\n");
  const turn = (v) => `#pedal set ${BLOCK} ${KNOB} ${v}\n`;

  await sleep(300);
  send(WATCH, 1); await reply(WATCH);
  send(STATE); const start = setting(await reply(STATE));
  const first = start === 30 ? 31 : 30, second = 77;
  sim.stdin.write(turn(first));
  let shown = setting(await reply(STATE));               // the push of the first change
  const mark = frames.length;
  sim.stdin.write(turn(second) + (withPing ? "f07d4f5012f7\n" : ""));
  await sleep(600);
  const sent = frames.slice(mark);
  for (const f of sent) if (f.cmd === STATE) shown = setting(f);
  send(STATE); const pedal = setting(await reply(STATE));
  sim.stdin.end();
  console.log(`${withPing ? "a PING in the 40 ms" : "no request in the 40 ms"}: the pedal then sent [${sent.map((f) => NAMES[f.cmd] || f.cmd).join(", ")}]; ` +
    `the editor shows ${shown}, the pedal has ${pedal}: ${shown === pedal ? "the same" : "the editor is stale"}`);
}

await run(true);
await run(false);
