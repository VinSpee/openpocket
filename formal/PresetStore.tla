---- MODULE PresetStore ----
\* SPDX-License-Identifier: GPL-3.0-only
\* The preset store and a power cut.
\*
\* The 52 slots are kept in RAM (st_slot) and written whole on each save, in turn to copy A and copy B, two
\* sectors each (firmware/px_store.h st_commit): both sectors erased, the slots of sector 1, the slots of sector
\* 0, the header last (sequence number, CRC16 of every slot), then read back. At the start st_load takes the
\* whole copy with the highest sequence.
\*
\* Promise: whenever the power goes, the next start loads the last sound the pedal confirmed as saved, or one
\* it was asked to save after that; never an older one, never nothing.
\*
\* A version number stands for the content of the 52 slots, and grows with each save. A sector that an erase or
\* a program was cut in holds junk, which never matches a header's CRC; a header cut in its program is not a
\* header (assumed: the magic, the format bytes and the CRC16 reject it).
EXTENDS Integers

CONSTANTS
  MaxSaves,          \* how many saves a run makes
  OverwriteCurrent   \* FALSE: the code (the copy not in use is written); TRUE: a mutation (the copy in use)

VARIABLES
  flash,    \* per copy: hdr (the header), s0 and s1 (the slots of sector 0 and 1)
  ram,      \* the version in st_slot
  cur,      \* st_cur: the copy in use, "none" before the first save
  seq,      \* st_seq
  pc,       \* "run", or the step of st_commit about to be made
  tgt,      \* the copy st_commit writes
  nsave,    \* saves begun so far (each has its own version)
  durable,  \* the version last confirmed: by a save that returned 0, or loaded at a start
  attempt,  \* the version of the save last begun
  lost      \* a start loaded a version older than durable

vars == <<flash, ram, cur, seq, pc, tgt, nsave, durable, attempt, lost>>

Copies == {"A", "B"}
Junk == -1                                \* a sector's slots: Junk, 0 (erased), or a version
E == [st |-> "E", seq |-> 0, v |-> 0]     \* a header: erased,
J == [st |-> "J", seq |-> 0, v |-> 0]     \* cut in its program or its erase,
Ok(s, v) == [st |-> "ok", seq |-> s, v |-> v]   \* or whole
Steps == {"e0", "e1", "w1", "w0", "wh", "vf"}

Other(c) == IF c = "A" THEN "B" ELSE "A"

\* st_load: a copy is whole when its header is one and its CRC is that of its slots
Whole(f, c) == f[c].hdr.st = "ok" /\ f[c].s0 = f[c].hdr.v /\ f[c].s1 = f[c].hdr.v
\* A first; B only with a higher sequence
Best(f) ==
  IF Whole(f, "A") /\ Whole(f, "B") THEN (IF f["B"].hdr.seq > f["A"].hdr.seq THEN "B" ELSE "A")
  ELSE IF Whole(f, "A") THEN "A"
  ELSE IF Whole(f, "B") THEN "B"
  ELSE "none"
Loaded(f) == IF Best(f) = "none" THEN 0 ELSE f[Best(f)].hdr.v

Init ==
  /\ flash = [c \in Copies |-> [hdr |-> E, s0 |-> 0, s1 |-> 0]]
  /\ ram = 0 /\ cur = "none" /\ seq = 0
  /\ pc = "run" /\ tgt = "A"
  /\ nsave = 0 /\ durable = 0 /\ attempt = 0 /\ lost = FALSE

\* st_save (or SLOT_PUT and COMMIT from the editor): the slots change in RAM, st_commit begins
Save ==
  /\ pc = "run" /\ nsave < MaxSaves
  /\ nsave' = nsave + 1 /\ ram' = nsave + 1 /\ attempt' = nsave + 1
  /\ tgt' = IF cur = "none" THEN "A" ELSE IF OverwriteCurrent THEN cur ELSE Other(cur)
  /\ pc' = "e0"
  /\ UNCHANGED <<flash, cur, seq, durable, lost>>

\* fl_erase4k(base): the header and sector 0's slots
Erase0 ==
  /\ pc = "e0" /\ pc' = "e1"
  /\ flash' = [flash EXCEPT ![tgt].hdr = E, ![tgt].s0 = 0]
  /\ UNCHANGED <<ram, cur, seq, tgt, nsave, durable, attempt, lost>>

\* fl_erase4k(base + 0x1000)
Erase1 ==
  /\ pc = "e1" /\ pc' = "w1"
  /\ flash' = [flash EXCEPT ![tgt].s1 = 0]
  /\ UNCHANGED <<ram, cur, seq, tgt, nsave, durable, attempt, lost>>

\* fl_write: slots 34..51
Write1 ==
  /\ pc = "w1" /\ pc' = "w0"
  /\ flash' = [flash EXCEPT ![tgt].s1 = ram]
  /\ UNCHANGED <<ram, cur, seq, tgt, nsave, durable, attempt, lost>>

\* fl_write: slots 0..33
Write0 ==
  /\ pc = "w0" /\ pc' = "wh"
  /\ flash' = [flash EXCEPT ![tgt].s0 = ram]
  /\ UNCHANGED <<ram, cur, seq, tgt, nsave, durable, attempt, lost>>

\* fl_write: the header, last
WriteHeader ==
  /\ pc = "wh" /\ pc' = "vf"
  /\ flash' = [flash EXCEPT ![tgt].hdr = Ok(seq + 1, ram)]
  /\ UNCHANGED <<ram, cur, seq, tgt, nsave, durable, attempt, lost>>

\* the read back is right: st_cur and st_seq move, st_commit returns 0
Verify ==
  /\ pc = "vf" /\ pc' = "run"
  /\ cur' = tgt /\ seq' = seq + 1 /\ durable' = ram
  /\ UNCHANGED <<flash, ram, tgt, nsave, attempt, lost>>

\* what the step about to be made can leave on the flash when it does not end: done, not begun, or cut
Partial ==
  CASE pc = "e0" -> { [flash EXCEPT ![tgt].hdr = h, ![tgt].s0 = b] :
                        h \in {flash[tgt].hdr, J, E}, b \in {flash[tgt].s0, Junk, 0} }
    [] pc = "e1" -> { [flash EXCEPT ![tgt].s1 = b] : b \in {flash[tgt].s1, Junk, 0} }
    [] pc = "w1" -> { [flash EXCEPT ![tgt].s1 = b] : b \in {0, Junk, ram} }
    [] pc = "w0" -> { [flash EXCEPT ![tgt].s0 = b] : b \in {0, Junk, ram} }
    [] pc = "wh" -> { [flash EXCEPT ![tgt].hdr = h] : h \in {E, J, Ok(seq + 1, ram)} }
    [] OTHER -> {flash}

\* the flash fails a step (an erase, a program, the read back): st_commit returns an error, st_cur stays
FlashError ==
  /\ pc \in Steps /\ pc' = "run"
  /\ flash' \in Partial
  /\ UNCHANGED <<ram, cur, seq, tgt, nsave, durable, attempt, lost>>

\* the power goes, at any moment; at the next start st_load takes the best whole copy
PowerCut ==
  \E f \in Partial :
    /\ flash' = f
    /\ cur' = Best(f) /\ seq' = (IF Best(f) = "none" THEN 0 ELSE f[Best(f)].hdr.seq)
    /\ ram' = Loaded(f)
    /\ lost' = (lost \/ Loaded(f) < durable)
    /\ durable' = Loaded(f)
    /\ pc' = "run"
    /\ UNCHANGED <<tgt, nsave, attempt>>

Next ==
  \/ Save \/ Erase0 \/ Erase1 \/ Write1 \/ Write0 \/ WriteHeader \/ Verify
  \/ FlashError \/ PowerCut

Spec == Init /\ [][Next]_vars

Sector == Junk..MaxSaves
Headers == [st : {"E", "J", "ok"}, seq : 0..MaxSaves, v : 0..MaxSaves]
TypeOK ==
  /\ flash \in [Copies -> [hdr : Headers, s0 : Sector, s1 : Sector]]
  /\ ram \in 0..MaxSaves /\ cur \in Copies \cup {"none"} /\ seq \in 0..MaxSaves
  /\ pc \in Steps \cup {"run"} /\ tgt \in Copies
  /\ nsave \in 0..MaxSaves /\ durable \in 0..MaxSaves /\ attempt \in 0..MaxSaves
  /\ lost \in BOOLEAN

\* The promise, for a cut that leaves the flash as it is now, and (lost) for every cut made so far.
NoLostSave == ~lost /\ Loaded(flash) >= durable /\ Loaded(flash) <= attempt

\* A stricter promise, which the code does not keep (formal/FINDINGS.md, N1): a start loads the last confirmed
\* save or the one in progress, and never a save that returned an error.
OnlyConfirmedOrInProgress == Loaded(flash) \in {durable, attempt}

\* Two whole copies never carry the same sequence: st_load's choice does not depend on its order.
NoTie == Whole(flash, "A") /\ Whole(flash, "B") => flash["A"].hdr.seq # flash["B"].hdr.seq

\* The copy in use is whole while no save runs.
InUseIsWhole == pc = "run" /\ cur # "none" => Whole(flash, cur)
====
