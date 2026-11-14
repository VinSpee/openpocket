// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — PALETTE (firmware/apps/palette, docs/PALETTE.md) in the hosted simulator (tools/sim.mjs --app palette).
// The program under test is the app's own C file, its app.h and its engine, compiled for this computer with tests/host
// as the chip; the base firmware's start and main loop, controls, audio stages, store, MIDI reader and product reply
// are the pedal's own code. Driven over the simulator's HTTP interface: who it says it is, its twenty factory sounds,
// each control, its sound at the two outputs, the reference's MIDI map, the MIDI clock, its own editor requests, a save
// that survives a restart and stays inside its flash range, a store that fails its check, safe mode, a guitar loop.
//   node tests/test_palette_sim.mjs
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { startSim } from "../tools/sim.mjs";
import { parseIdentity, parseInfo, IS_ALT } from "../docs/pxota.js";

const TMP = mkdtempSync(join(tmpdir(), "oppal-"));
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(110)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const flash = join(TMP, "flash.bin");
let sim = await startSim({ port: 0, flash, app: "palette" });
const get = async (path) => (await fetch(sim.url + path)).json();
const act = async (path, body) => { const r = await fetch(sim.url + path, { method: "POST", body }); assert.equal(r.status, 200, `${path}: ${await r.clone().text()}`); return r.json(); };
const sysex = async (hex) => { const { reply } = await act("/api/sysex", hex); return reply ? Uint8Array.from(Buffer.from(reply, "hex")) : null; };
const hx = (...b) => b.map((v) => v.toString(16).padStart(2, "0")).join("");
const req = (cmd, ...a) => sysex("f07d4f50" + hx(cmd, ...a) + "f7");
const send = async (...b) => { const r = await fetch(sim.url + "/api/midi", { method: "POST", body: hx(...b) }); assert.equal(r.status, 204); };
const midi = async (...b) => { await send(...b); await act("/api/wait/5"); };   // (a message, then the passes that read it)
const press = (name) => act(`/api/press/${name}`);
const knob = (n, fast) => act(`/api/knob/${n}${fast ? "?fast" : ""}`);
const KNOB = ["tilt", "amtChar", "rate", "amtMove", "time", "amtDiff", "mix", "amtText", "sens", "volChar", "driftMove", "volMove", "driftDiff", "volDiff", "level", "volText"];
// the app's own STATE request (app.h)
async function state() {
  const m = await req(5), a = [...m.slice(5, -1)];
  const s = { slot: a[0], dirty: a[1] & 1, saved: a[1] >> 1 & 1, bypass: a[1] >> 2 & 1, clock: a[1] >> 3 & 1, page: a[2], line: a[3],
    name: String.fromCharCode(...a.slice(4, 16)).replace(/\0.*$/, ""), knob: a.slice(16, 32), fx: a.slice(32, 36), order: a.slice(36, 40),
    on: a[40], style: a[41], tempo: a[42] + 128 * a[43], bytes: a.length };
  KNOB.forEach((k, i) => { s[k] = s.knob[i]; });
  return s;
}
const level = () => act("/api/sound/440");                     // a sine as the guitar: the two outputs' RMS, of full scale
const near = (v, want, tol) => Math.abs(v - want) <= tol;
const dry = 0.25 * 0.5 * Math.SQRT1_2 * 0.64;                   // a quarter of full scale, halved at the input, RMS, MASTER 80
const C = 1, M = 2, D = 4, T = 8;

// ---- who it is ----
const info = await get("/api/info");
let st = await get("/api/state");
ok(info.kind === "app" && info.app === "palette" && st.name === "PALETTE" && st.version === "v01" && st.identity === "PocketX_901", `the simulator runs ${st.name} ${st.version}, identity ${st.identity}`);
const p = parseInfo(await req(1));
ok(p && IS_ALT(p) && p.app === "palette" && p.name === "PALETTE" && p.proto === 0 && p.caps.join() === "stereo,midi,store" && p.editor === 1,
  "INFO says it is not OpenPocket (protocol 0), which product it is, and what it can do: stereo, midi, store; its own editor protocol 1");
ok((await sysex("f07d4f50020000f7")) === null, "a request of OpenPocket's editor that it does not have gets no reply");
await act("/api/wait/6200");
st = await get("/api/state");
ok(st.hold === 0 && st.guard_pending === 0 && st.start === "OK" && st.safe === 0, "6 s on: its sound is on, and the boot guard is told that the start finished");

// ---- the factory sounds, the empty slots ----
let s = await state();
ok(s.bytes === 44 && s.slot === 0 && s.name === "CLEAN ROOM" && s.on === (C | D) && s.fx[0] === 1 && s.fx[2] === 2 && !s.dirty && !s.saved && s.page === 0 && s.line === 0,
  "it starts on its first factory sound: CLEAN ROOM, Sweeten and Space on, the sound's page");
{
  const names = [], bad = [];
  for (let k = 0; k < 20; k++) {
    await midi(0xC0, k);
    s = await state();
    names.push(s.name);
    const l = await level();
    if (s.slot !== k || !s.name || s.dirty || !s.on || s.knob.some((v) => v > 100) || [...s.order].sort().join() !== "0,1,2,3" || !(l.left > 0.002 && l.left < 0.4 && l.right > 0.002 && l.right < 0.4)) bad.push(`${k + 1} ${s.name} ${l.left} ${l.right}`);
  }
  ok(bad.length === 0 && new Set(names).size === 20, `Program Change 0..19: twenty factory sounds, each named, in range, and heard on both outputs${bad.length ? ": " + bad.join("; ") : ""}`);
  ok(s.name === "FULL CHAIN" && s.on === 15 && s.tempo === 120, "the last one has the four modules on and a tempo of 120");
  s = await state(); await midi(0xC0, 17); s = await state();
  ok(s.name === "ECHO>FUZZ" && s.order.join() === "2,0,1,3", "one of them has another order: Diffusion before Character");
  await midi(0xC0, 20);
  s = await state();
  const l = await level();
  ok(s.slot === 20 && s.name === "EMPTY" && s.on === 0 && near(l.left, dry, 0.003) && near(l.right, dry, 0.003), `slot 21 is empty: every module off, the guitar on both outputs (${l.left}, ${l.right}; dry is ${dry.toFixed(4)})`);
  await midi(0xC0, 80); await midi(0x90, 60, 64);
  ok((await state()).slot === 20, "a Program Change beyond the 80 slots and a note change nothing");
}

// ---- the controls: a module's button, the knob, PUSH, HOME ----
await press("amp");
s = await state();
ok(s.page === 1 && s.line === 1 && s.on === 0, "AMP: Character's page, on its AMOUNT line; nothing of the sound changes");
await knob(7);
s = await state();
ok(s.amtChar === 57 && s.dirty === 1, "the knob: AMOUNT 57, a step a detent; the sound is marked as changed");
await knob(-4, true);
ok((await state()).amtChar < 45, "turned quickly: steps of 5");
await press("amp");
s = await state();
ok(s.fx[0] === 1 && s.on === C, "AMP again: Character's next effect (Sweeten), and the module is on");
for (let k = 0; k < 4; k++) await press("amp");
ok((await state()).fx[0] === 0, "four more times: back to its first effect, Drive");
await press("amp_long");
s = await state(); let l = await level();
ok(s.on === 0 && s.fx[0] === 0 && near(l.left, dry, 0.003), "AMP held: Character off, the guitar as it is; the module keeps its effect");
await press("amp_long");
ok((await state()).on === C, "held again: on");
await press("push"); await press("push"); await knob(-10, true);
s = await state();
ok(s.line === 3 && s.sens < 50 && s.tilt === 50, "PUSH twice: the SENSITIVITY line; the knob changes it and nothing else");
for (let k = 0; k < 2; k++) await press("push");
ok((await state()).line === 0, "PUSH past the last line: the first");
await knob(1);
ok((await state()).fx[0] === 1, "the knob on the EFFECT line: the next effect");
await press("dly"); await press("mod"); await press("cab");
s = await state();
ok(s.page === 4 && s.on === C, "DLY, MOD, CAB: Diffusion's, Movement's, Texture's page, without a change to the sound");
await press("push");
await knob(2);
s = await state();
ok(s.line === 2 && s.style === 2, "Texture's third line is the filter's STYLE: HIGH-PASS");
await press("cab_long"); for (let k = 0; k < 3; k++) await press("push"); await knob(10, true);
s = await state(); l = await level();
ok(s.on === (C | T) && s.amtText > 90 && l.left < 0.3 * dry && l.right < 0.3 * dry, `Texture on, its AMOUNT up: the high-pass filter takes the 440 Hz guitar away (${l.left} of ${dry.toFixed(4)})`);
await press("cab_long");
await press("home");
s = await state();
ok(s.page === 0 && s.line === 0, "HOME: the sound's page, on PRESET");
await knob(1);
s = await state();
ok(s.slot === 21 && s.name === "EMPTY", "the knob on PRESET reaches every slot, the empty ones too");

// ---- MOVE: a module along the chain ----
await midi(0xC0, 1);                                            // WARM DRIVE: Drive, then Cascade
const before = Buffer.from(await (await fetch(sim.url + "/api/screen.rgb565")).arrayBuffer());
await press("fx");
ok((await state()).order.join() === "0,1,2,3", "FX on the sound's page moves nothing: a module's page first");
await press("dly"); await press("fx"); await knob(-2);
s = await state();
ok(s.order.join() === "2,0,1,3" && s.dirty === 1, "DLY, FX, the knob two detents back: Diffusion is first in the chain");
await knob(-3);
ok((await state()).order.join() === "2,0,1,3", "it stops at the chain's end");
await press("push"); await knob(3);
s = await state();
ok(s.order.join() === "2,0,1,3" && s.amtDiff === 28, "PUSH drops it: the knob is AMOUNT's again");
ok(!before.equals(Buffer.from(await (await fetch(sim.url + "/api/screen.rgb565")).arrayBuffer())), "the screen was drawn again");

// ---- the tempo: TAP, the TEMPO line, the MIDI clock ----
await press("rev"); await act("/api/wait/400"); await press("rev");
s = await state();
ok(s.tempo >= 95 && s.tempo <= 121, `REV twice, half a second apart or a little more: a tempo of about 120 (${s.tempo})`);
await press("rev_long");
ok((await state()).tempo === 0, "REV held: free time");
await press("home"); for (let k = 0; k < 3; k++) await press("push");
await knob(1); s = await state();
ok(s.line === 3 && s.tempo === 30, "the knob on the TEMPO line: from free time to 30");
await knob(-1);
ok((await state()).tempo === 0, "and back to free time");
await act("/api/clock/60/20");                                  // 24 clocks a quarter note, 21 ms apart: 504 ms
s = await state();
ok(s.clock === 1 && s.tempo === 0, "a MIDI clock: it sets the tempo while it runs, and the preset's own tempo stays");
await act("/api/wait/700");
ok((await state()).clock === 0, "the clock stops coming: the preset's tempo again");
await act("/api/clock/60/20"); await midi(0xFC);
ok((await state()).clock === 0, "a clock, then Stop: the same at once");
await midi(0xB0, 93, 127); await act("/api/wait/495"); await midi(0xB0, 93, 127);
s = await state();
ok(s.tempo >= 95 && s.tempo <= 121, `Control Change 93 twice, half a second apart or a little more: a tap (${s.tempo})`);

// ---- the footswitches: the presets that hold a sound; held: bypass ----
await midi(0xC0, 19);
await press("fs_up");
ok((await state()).slot === 0, "the right footswitch past the last factory sound: the first, the empty slots skipped");
await press("fs_dn");
ok((await state()).slot === 19, "the left footswitch: back to the last");
await press("fs_up_long");
s = await state(); l = await level();
ok(s.bypass === 1 && s.slot === 19 && near(l.left, dry, 0.003) && near(l.right, dry, 0.003), "a footswitch held: bypass, the guitar on both outputs; the preset stays");
await press("fs_dn_long");
s = await state(); l = await level();
ok(s.bypass === 0 && l.left > 0.005 && l.right > 0.005, "held again: engaged");

// ---- MIDI: the reference's map ----
await midi(0xC0, 20);                                           // an empty slot: every knob neutral
for (let k = 0; k < 16; k++) await midi(0xB0, 64 + k, 10 + 7 * k);
s = await state();
ok(s.knob.every((v, k) => v === Math.round((10 + 7 * k) * 100 / 127)), "Control Change 64..79: the sixteen knob functions, each its own");
ok(s.tilt === 8 && s.amtChar === 13 && s.mix === 41 && s.level === 85 && s.volText === 91, "TILT is 64, Character's AMOUNT 65, MIX 70, OUTPUT LEVEL 78, Texture's EFFECT VOL 79");
await midi(0xB0, 16, 50); await midi(0xB0, 17, 0); await midi(0xB0, 18, 109); await midi(0xB0, 19, 66);
s = await state();
ok(s.fx.join() === "2,0,4,3" && s.on === 15, "Control Change 16..19: Fuzz, Doubler, Reverse, Broken, each module on");
await midi(0xB0, 17, 110); await midi(0xB0, 106, 0);
s = await state();
ok(s.on === (C | D) && s.fx.join() === "2,0,4,3", "17 at 110: Movement off; 106 below 64: Texture off; both keep their effect");
await midi(0xB0, 104, 127);
ok((await state()).on === (C | M | D), "104 at 127: Movement on again");
await midi(0xB0, 84, 0); s = await state(); await midi(0xB0, 84, 60); const s2 = await state(); await midi(0xB0, 84, 127);
ok(s.style === 1 && s2.style === 0 && (await state()).style === 2, "Control Change 84: the filter style, low-pass, tilt, high-pass");
await midi(0xB0, 91, 0);
ok((await state()).bypass === 1, "Control Change 91 below 64: bypass");
await midi(0xB0, 91, 127);
ok((await state()).bypass === 0, "at 127: engaged");
await midi(0xB0, 70, 0);
l = await level();
ok(near(l.left, dry * 10 ** ((85 - 50) * 0.24 / 20), 0.02), `MIX at 0: the guitar, at the OUTPUT LEVEL (${l.left})`);
for (let k = 0; k < 120; k++) await send(0xB0, 70, 127);   // more than the ring holds at once: it is read every pass
ok(parseInfo(await req(1)) !== null, "after 120 more messages the pedal still answers SysEx: the ring is read");

// ---- its own editor requests ----
await req(7, 20);
let m = await req(6, 6, 30);
ok(m && m[4] === 6 && m[5] === 6 && m[6] === 0 && (await state()).mix === 30, "SET knob 6: MIX 30, answered as taken");
m = await req(6, 0, 127);
ok(m && m[6] === 0 && (await state()).tilt === 100, "a value above 100 is taken as 100");
m = await req(6, 18, 3); s = await state();
ok(m[6] === 0 && s.fx[2] === 3 && s.on === D, "SET 18: Diffusion's effect, Collage, and the module on");
m = await req(6, 18, 5);
ok(m[6] === 1 && (await state()).fx[2] === 3, "an effect that does not exist: refused, nothing changes");
await req(6, 22, 0);
ok((await state()).on === 0, "SET 22: Diffusion off");
m = await req(6, 25, 172 & 127, 172 >> 7);
ok(m[6] === 0 && (await state()).tempo === 172, "SET 25: the tempo, 172, in two bytes");
m = await req(6, 27, 3, 2, 1, 0);
ok(m[6] === 0 && (await state()).order.join() === "3,2,1,0", "SET 27: the order");
m = await req(6, 27, 3, 3, 1, 0);
ok(m[6] === 1 && (await state()).order.join() === "3,2,1,0", "an order that is not one: refused");
m = await req(6, 26, 1);
ok((await state()).bypass === 1, "SET 26: bypass");
await req(6, 26, 0);
m = await req(9, 4);
ok(m && m[5] === 4 && m[6] === 1 && String.fromCharCode(...m.slice(7, -2)) === "BIG SPACE", "NAME 4: the slot holds a factory sound, BIG SPACE");
m = await req(9, 40);
ok(m && m[6] === 0, "NAME 40: an empty slot");
m = await req(11);
ok(m && m.length === 14 && m[4] === 11 && m[7] === 0 && m[8] === 0 && m[11] === 0 && m[12] === 0, "LOAD: the load, its peak, no late block, no NaN, no overload bypass");

// ---- a save, a restart ----
await req(7, 30);
await req(6, 16, 3); await req(6, 1, 77);                       // Howl, AMOUNT 77
await req(10, ...Buffer.from("MY HOWL"));
s = await state();
ok(s.slot === 30 && s.name === "MY HOWL" && s.dirty === 1 && s.saved === 0, "slot 31, changed and named over its editor requests: not saved yet");
await press("home"); await press("push_long");
s = await state();
ok(s.dirty === 0 && s.saved === 1, "PUSH held: saved");
await req(7, 31); await req(6, 19, 2);
m = await req(8);
s = await state();
ok(m && m[5] === 0 && s.saved === 1 && s.name === "USER 32", "its SAVE request on an empty slot: saved, and named after the slot");
st = await get("/api/state");
ok(st.writes_ok === true, "every flash write was inside the app's own range, with the sound held");
await press("fs_up");
ok((await state()).slot === 0, "the footswitch from the last saved slot: the first factory sound");
await press("fs_dn"); await press("fs_dn");
ok((await state()).slot === 30, "and back twice: the two saved slots are reached, the empty ones between skipped");
await sim.close();
let nor = readFileSync(flash);
const outside = [...nor.subarray(0, 0xE5000), ...nor.subarray(0xEF000)].every((b) => b === 0xFF);
ok(nor.length === 0x100000 && outside, "the flash outside 0xE5000..0xEF000 is untouched: not the firmware, not OpenPocket's presets, models or IRs");
sim = await startSim({ port: 0, flash, app: "palette" });
s = await state();
ok(s.slot === 31 && s.name === "USER 32" && s.fx[3] === 2 && s.saved === 1 && !s.dirty, "after a restart it starts on the slot saved last, with the saved sound");
await req(7, 30); s = await state();
ok(s.name === "MY HOWL" && s.fx[0] === 3 && s.amtChar === 77, "the other saved slot is there too");
await req(7, 0);
ok((await state()).name === "CLEAN ROOM", "and the factory sounds are as they were");
await sim.close();

// ---- a store whose copy is whole but holds a sound out of range: not loaded ----
{
  const crc16 = (buf, c) => { for (const b of buf) { c ^= b << 8; for (let i = 0; i < 8; i++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF; } return c; };
  nor = Buffer.from(readFileSync(flash));
  const copies = [0xE5000, 0xEA000].filter((b) => nor.readUInt32LE(b) === 0x544C4150).sort((a, b) => nor.readUInt32LE(b + 4) - nor.readUInt32LE(a + 4));
  const base = copies[0], len = nor.readUInt32LE(base + 8), sum = (at) => crc16(nor.subarray(at + 16, at + 16 + len), crc16(nor.subarray(at, at + 12), nor[at + 14]));
  ok(copies.length === 2 && len === 12 + 80 * 40 && sum(base) === nor.readUInt16LE(base + 12) && nor.subarray(base + 0xF00, base + 0x1000).every((b) => b === 0xFF),
    "its store: two copies under its own magic, 3212 bytes, the checksum as the base makes it, each sector's last 256 bytes erased");
  const img = Buffer.from(nor);                                 // slot 31's first knob: 200, the checksum made right again
  for (const b of copies) {
    img[b + 16 + 12 + 30 * 40 + 12] = 200;
    img.writeUInt16LE(crc16(img.subarray(b + 16, b + 16 + len), crc16(img.subarray(b, b + 12), img[b + 14])), b + 12);
  }
  writeFileSync(flash, img);
  sim = await startSim({ port: 0, flash, app: "palette" });
  s = await state();
  ok(s.slot === 0 && s.name === "CLEAN ROOM" && s.saved === 0, "a store that passes its checksum but holds a knob at 200: not loaded, the factory sounds stand");
  await req(7, 30);
  ok((await state()).name === "EMPTY", "nothing of it is taken, not the slots that were in range either");
  ok(parseInfo(await req(1)) !== null && (await get("/api/state")).writes_ok === true, "and the pedal runs: USB answers");
  await sim.close();
}

// ---- a NaN, safe mode, a guitar loop ----
sim = await startSim({ port: 0, flash: join(TMP, "flash2.bin"), app: "palette" });
await act("/api/wait/2000");
await midi(0xC0, 4);
await act("/api/nan");
st = await get("/api/state"); l = await level();
ok(st.nans === 1 && st.nan_now === 0 && l.left > 0.01 && l.right > 0.01, "a NaN in the sound: counted, the engine cleared by the main loop, and the sound is back");
{
  const pull = async (ms) => new Int16Array(await (await fetch(`${sim.url}/api/audio/${ms}`, { method: "POST" })).arrayBuffer());
  ok((await fetch(sim.url + "/api/loop/nosuch", { method: "POST" })).status === 404, "a loop that is not in assets/loops: refused");
  await act("/api/loop/riff");
  let n = 0, e = [0, 0], diff = 0;
  for (let k = 0; k < 6; k++) {
    const a = await pull(250);
    n += a.length / 2;
    for (let i = 0; i < a.length; i += 2) { e[0] += a[i] * a[i]; e[1] += a[i + 1] * a[i + 1]; diff += Math.abs(a[i] - a[i + 1]); }
  }
  ok(n > 60000 && n < 70000 && Math.sqrt(e[0] / n) > 50 && Math.sqrt(e[1] / n) > 50 && diff > 0, `a guitar loop through BIG SPACE, pulled in six parts: ${n} frames of sound on both outputs, and the two sides differ`);
  await pull(0);
  await act("/api/loop/none");
  l = await level();
  ok(l.left > 0.01, "the loop off: the tone and the pedal's own time again");
}
await sim.close();
sim = await startSim({ port: 0, flash: join(TMP, "flash2.bin"), app: "palette", failedStart: true });
await act("/api/wait/7000");
st = await get("/api/state"); l = await level();
ok(st.safe === 1 && st.hold === 1 && near(l.left, dry, 0.003) && near(l.right, dry, 0.003), "after a start that did not finish: safe mode, the engine off, the guitar on both outputs");
ok(parseInfo(await req(1)) !== null && parseIdentity(await sysex("f0003245000000407ff7")) !== null, "and USB answers: the installer can put any firmware on it");
await sim.close();

console.log(failed ? `\n${failed} FAILED` : "\nall palette simulator tests passed");
process.exit(failed ? 1 : 0);
