---- MODULE EditorWatch ----
\* SPDX-License-Identifier: GPL-3.0-only
\* The web editor and the pedal's own controls.
\*
\* While the editor watches, the pedal pushes a STATE frame when its sound changes by its own controls, at most
\* one every 40 ms (firmware/px_editor.h ed_service). It knows what the editor has by a hash of the state
\* (ed_w.hash), and it does not push back a change the editor made itself: ed_handle ends with
\* ed_w.hash = ed_state_hash(). The editor (docs/pxedit.js, docs/editor.js, docs/edsound.js) sends one request
\* at a time, a PING every second, and asks for the state after each change of its own.
\*
\* Promise: once nothing is in flight and no push is owed, the editor shows the pedal's sound.
\*
\* One setting stands for the whole preset. The USB link is taken as reliable and in order (the editor's
\* timeout and its one retry are left out).
EXTENDS Integers, Sequences

CONSTANTS
  Values,        \* the values the setting takes
  MaxKnobs,      \* changes made on the pedal itself (a knob, a footswitch, a MIDI message)
  MaxPings,      \* PINGs the editor sends (docs/editor.js: one a second)
  MaxSets,       \* changes made in the editor
  KeepPending    \* FALSE: the code (every command the pedal answers sets ed_w.hash to the state now);
                 \* TRUE: the suggested fix (a command that does not send the state keeps a push that is owed)

VARIABLES
  pedal,    \* the setting on the pedal (PRESET)
  hash,     \* ed_w.hash: the state the editor is taken to have (Stale: none, the next push is owed)
  window,   \* TRUE: 40 ms have passed since the last push (now - ed_w.push_ms >= 40)
  inbox,    \* the request received and not handled yet (firmware/bsp/chip/usb.c sx_frame holds one)
  wire,     \* the frames on their way from the pedal to the editor, in order
  editor,   \* the setting as the editor shows it (app.st)
  waiting,  \* the command the editor waits a reply to (Editor.waiting), or "none"
  knobs, pings, sets   \* how many of each are still to come

vars == <<pedal, hash, window, inbox, wire, editor, waiting, knobs, pings, sets>>

None == [cmd |-> "none", v |-> 0]
Stale == -1
Frame(c, v) == [cmd |-> c, v |-> v]

Init ==
  /\ pedal = 0 /\ hash = 0 /\ editor = 0          \* just connected: STATE read, WATCH on
  /\ window = TRUE
  /\ inbox = None /\ wire = <<>> /\ waiting = "none"
  /\ knobs = MaxKnobs /\ pings = MaxPings /\ sets = MaxSets

\* ---- the pedal's main loop (firmware/openpocket.c app_main) ----
\* a control changes the sound (ui_turn, ui_buttons, px_midi_service)
Knob(v) ==
  /\ knobs > 0 /\ v # pedal
  /\ pedal' = v /\ knobs' = knobs - 1
  /\ UNCHANGED <<hash, window, inbox, wire, editor, waiting, pings, sets>>

\* 40 ms pass
Elapse ==
  /\ ~window /\ window' = TRUE
  /\ UNCHANGED <<pedal, hash, inbox, wire, editor, waiting, knobs, pings, sets>>

\* ed_service, first half: the state differs from the hash and the 40 ms are over: a STATE frame
PushDue == window /\ pedal # hash
Push ==
  /\ PushDue
  /\ hash' = pedal /\ window' = FALSE
  /\ wire' = Append(wire, Frame("STATE", pedal))
  /\ UNCHANGED <<pedal, inbox, editor, waiting, knobs, pings, sets>>

\* ed_service, second half, in the same pass after the push check: ed_handle answers the request.
\* A STATE reply carries the whole state, so the editor has it. Any other reply does not: with the fix, a change
\* of the pedal's that has not been pushed yet stays owed.
Handle ==
  /\ inbox.cmd # "none" /\ ~PushDue
  /\ LET now == IF inbox.cmd = "SET" THEN inbox.v ELSE pedal
         owed == hash # pedal
     IN /\ pedal' = now
        /\ hash' = IF KeepPending /\ owed /\ inbox.cmd # "STATE" THEN Stale ELSE now
        /\ wire' = Append(wire, Frame(inbox.cmd, now))
  /\ inbox' = None
  /\ UNCHANGED <<window, editor, waiting, knobs, pings, sets>>

\* ---- the editor (docs/pxedit.js Editor.request: one request at a time) ----
\* docs/editor.js ping()
SendPing ==
  /\ waiting = "none" /\ pings > 0
  /\ inbox' = Frame("PING", 0) /\ waiting' = "PING" /\ pings' = pings - 1
  /\ UNCHANGED <<pedal, hash, window, wire, editor, knobs, sets>>

\* docs/edsound.js sendParam: shown at once, then SET
SendSet(v) ==
  /\ waiting = "none" /\ sets > 0
  /\ editor' = v
  /\ inbox' = Frame("SET", v) /\ waiting' = "SET" /\ sets' = sets - 1
  /\ UNCHANGED <<pedal, hash, window, wire, knobs, pings>>

\* Editor._onMessage: a frame with the awaited command is the reply; any other STATE frame is a push (onPush).
\* After SET's reply the editor asks for the state (edsound.js flushParam -> sync).
Recv ==
  /\ wire # <<>>
  /\ LET m == Head(wire) IN
       IF waiting # "none" /\ m.cmd = waiting
         THEN IF m.cmd = "SET"
                THEN /\ waiting' = "STATE" /\ inbox' = Frame("STATE", 0)
                     /\ UNCHANGED editor
                ELSE /\ waiting' = "none"
                     /\ editor' = IF m.cmd = "STATE" THEN m.v ELSE editor
                     /\ UNCHANGED inbox
         ELSE /\ editor' = IF m.cmd = "STATE" THEN m.v ELSE editor
              /\ UNCHANGED <<waiting, inbox>>
  /\ wire' = Tail(wire)
  /\ UNCHANGED <<pedal, hash, window, knobs, pings, sets>>

Next ==
  \/ \E v \in Values : Knob(v) \/ SendSet(v)
  \/ Elapse \/ Push \/ Handle \/ SendPing \/ Recv

Spec ==
  /\ Init /\ [][Next]_vars
  /\ WF_vars(Elapse) /\ WF_vars(Push) /\ WF_vars(Handle) /\ WF_vars(Recv)

Cmds == {"STATE", "PING", "SET"}
TypeOK ==
  /\ pedal \in Values /\ hash \in Values \cup {Stale} /\ editor \in Values
  /\ window \in BOOLEAN
  /\ inbox \in [cmd : Cmds \cup {"none"}, v : Values]
  /\ wire \in Seq([cmd : Cmds, v : Values]) /\ Len(wire) <= MaxKnobs + MaxPings + 2 * MaxSets
  /\ waiting \in Cmds \cup {"none"}
  /\ knobs \in 0..MaxKnobs /\ pings \in 0..MaxPings /\ sets \in 0..MaxSets

\* The promise: nothing in flight, no push owed, and the editor shows the pedal's sound.
Quiet == wire = <<>> /\ inbox.cmd = "none" /\ waiting = "none" /\ hash = pedal
InSync == Quiet => editor = pedal

\* And it gets there: after the last change the editor shows the pedal's sound for good.
Converges == <>[](editor = pedal)
====
