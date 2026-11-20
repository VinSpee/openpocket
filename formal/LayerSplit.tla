---- MODULE LayerSplit ----
\* SPDX-License-Identifier: GPL-3.0-only
\* The NAM network split between the two cores by layers.
\*
\* CPU0 runs the input and layers 0..s of each 32-frame block and hands the block over (engine/px_nam.c
\* pxn_lp_a); CPU1 runs layers s+1.. and the head, up to PXN_LPQ blocks behind (pxn_lp_b). s may change from one
\* block to the next (pxn_lp_split). A layer reads its own ring and writes the next layer's; layer s writes the
\* block's hand-over instead, which CPU1 copies into layer s+1's ring. ld[i] counts the blocks layer i has done:
\* CPU0 takes a layer CPU1 had for the block before only once ld says CPU1 is done with it.
\*
\* Promise: each ring belongs to one core at a time, and every layer runs every block once, in order.
EXTENDS Naturals

CONSTANTS
  NL,          \* layers (the pedal: 23)
  NB,          \* blocks a run hands over
  LPQ,         \* blocks CPU1 may be behind (PXN_LPQ)
  CheckFree    \* TRUE: the code; FALSE: a mutation (CPU0 takes the layers asked without looking at ld)

VARIABLES
  lin, lout,   \* pxn_lp_in, pxn_lp_out: blocks handed over, finished
  ld,          \* pxn_lp->ld: per layer, blocks done
  want,        \* pxn_lp_s: the split asked for
  hard,        \* pxn_lp_hard: the next rise may wait for the other core (pxn_lp_force)
  es,          \* e->s of each queue entry: the split its block was handed over with
  apc, as, ai, \* CPU0 in pxn_lp_a: "idle", "wait", "run" (in layer ai, of 0..as), "hand"
  bpc, bi,     \* CPU1 in pxn_lp_b: "idle", "copy" (the hand-over into ring bi), "run" (in layer bi), "head"
  bad          \* a layer ran a block out of its turn

vars == <<lin, lout, ld, want, hard, es, apc, as, ai, bpc, bi, bad>>

Layers == 0..(NL - 1)

Init ==
  /\ lin = 0 /\ lout = 0
  /\ ld = [i \in Layers |-> 0]
  /\ want \in Layers /\ hard = FALSE
  /\ es = [q \in 0..(LPQ - 1) |-> 0]
  /\ apc = "idle" /\ as = 0 /\ ai = 0
  /\ bpc = "idle" /\ bi = 0
  /\ bad = FALSE

\* ---- CPU0 ----
\* between two blocks: px_nam_lp_split, px_nam_lp_force (firmware/px_audio.h lp_split, dsp_lp_run)
Split(s) ==
  /\ apc = "idle" /\ want' = s
  /\ UNCHANGED <<lin, lout, ld, hard, es, apc, as, ai, bpc, bi, bad>>
Force ==
  /\ apc = "idle" /\ hard' = TRUE
  /\ UNCHANGED <<lin, lout, ld, want, es, apc, as, ai, bpc, bi, bad>>

\* CPU1 has finished layer k of the block before this one
Free(k) == ld[k] >= lin
\* "while (s < pxn_lp_s && ld[s + 1] - b >= 0) s++", read at one instant
Top == CHOOSE s \in 0..want : (\A k \in 1..s : Free(k)) /\ (s = want \/ ~Free(s + 1))

\* pxn_lp_a begins block lin: the queue has room, then the split for this block. ld only grows, so the loop,
\* which reads it layer by layer while CPU1 works, ends at Top or below: any s up to Top is taken here.
AStart ==
  /\ apc = "idle" /\ lin < NB /\ lin - lout < LPQ
  /\ \E s \in (IF CheckFree THEN 0..Top ELSE {want}) :
       IF CheckFree /\ s < want /\ hard
         THEN apc' = "wait" /\ as' = want /\ UNCHANGED es
         ELSE apc' = "run" /\ as' = s /\ es' = [es EXCEPT ![lin % LPQ] = s]
  /\ ai' = 0 /\ hard' = FALSE
  /\ UNCHANGED <<lin, lout, ld, want, bpc, bi, bad>>

\* pxn_lp_wait_a: forced up, CPU0 waited for CPU1 to finish those layers of the block before
AWaited ==
  /\ apc = "wait" /\ \A j \in 0..as : Free(j)
  /\ apc' = "run" /\ es' = [es EXCEPT ![lin % LPQ] = as]
  /\ UNCHANGED <<lin, lout, ld, want, hard, as, ai, bpc, bi, bad>>

\* layer ai of block lin is done: pxn_range_cpu, pxn_lp_next, ld[i] = b + 1
ALayer ==
  /\ apc = "run"
  /\ ld' = [ld EXCEPT ![ai] = lin + 1]
  /\ bad' = (bad \/ ld[ai] # lin)
  /\ IF ai < as THEN ai' = ai + 1 /\ UNCHANGED apc
               ELSE apc' = "hand" /\ UNCHANGED ai
  /\ UNCHANGED <<lin, lout, want, hard, es, as, bpc, bi>>

\* pxn_lp_in = b + 1
AHand ==
  /\ apc = "hand" /\ apc' = "idle" /\ lin' = lin + 1
  /\ UNCHANGED <<lout, ld, want, hard, es, as, ai, bpc, bi, bad>>

\* ---- CPU1 (or CPU0 when it is alone) ----
\* pxn_lp_b begins block lout: its split, pxn_lp_wait for the layers CPU0 had for the block before (CPU0 has
\* finished that block before it hands this one over, so the wait never holds CPU1 back)
BStart ==
  /\ bpc = "idle" /\ lout < lin
  /\ LET s == es[lout % LPQ] IN
       /\ \A j \in (s + 1)..(NL - 1) : ld[j] >= lout
       /\ IF s < NL - 1 THEN bpc' = "copy" /\ bi' = s + 1
                        ELSE bpc' = "head" /\ UNCHANGED bi
  /\ UNCHANGED <<lin, lout, ld, want, hard, es, apc, as, ai, bad>>

\* the hand-over is copied into layer bi's ring
BCopied ==
  /\ bpc = "copy" /\ bpc' = "run"
  /\ UNCHANGED <<lin, lout, ld, want, hard, es, apc, as, ai, bi, bad>>

\* layer bi of block lout is done
BLayer ==
  /\ bpc = "run"
  /\ ld' = [ld EXCEPT ![bi] = lout + 1]
  /\ bad' = (bad \/ ld[bi] # lout)
  /\ IF bi < NL - 1 THEN bi' = bi + 1 /\ UNCHANGED bpc
                    ELSE bpc' = "head" /\ UNCHANGED bi
  /\ UNCHANGED <<lin, lout, want, hard, es, apc, as, ai>>

\* the head and the output are made: pxn_lp_out = b + 1
BFinish ==
  /\ bpc = "head" /\ bpc' = "idle" /\ lout' = lout + 1
  /\ UNCHANGED <<lin, ld, want, hard, es, apc, as, ai, bi, bad>>

Next ==
  \/ \E s \in Layers : Split(s)
  \/ Force \/ AStart \/ AWaited \/ ALayer \/ AHand
  \/ BStart \/ BCopied \/ BLayer \/ BFinish

\* Both cores keep working: CPU0 begins the next block while one is left, and each step begun ends.
Spec ==
  /\ Init /\ [][Next]_vars
  /\ WF_vars(AStart) /\ WF_vars(AWaited) /\ WF_vars(ALayer) /\ WF_vars(AHand)
  /\ WF_vars(BStart) /\ WF_vars(BCopied) /\ WF_vars(BLayer) /\ WF_vars(BFinish)

TypeOK ==
  /\ lin \in 0..NB /\ lout \in 0..NB
  /\ ld \in [Layers -> 0..NB]
  /\ want \in Layers /\ hard \in BOOLEAN
  /\ es \in [0..(LPQ - 1) -> Layers]
  /\ apc \in {"idle", "wait", "run", "hand"} /\ as \in Layers /\ ai \in Layers
  /\ bpc \in {"idle", "copy", "run", "head"} /\ bi \in Layers
  /\ bad \in BOOLEAN

\* the rings each core is reading or writing now
TouchA == IF apc = "run" THEN {ai} \cup (IF ai < as THEN {ai + 1} ELSE {}) ELSE {}
TouchB ==
  IF bpc = "copy" THEN {bi}
  ELSE IF bpc = "run" THEN {bi} \cup (IF bi < NL - 1 THEN {bi + 1} ELSE {})
  ELSE {}

\* The promise.
RingExclusive == TouchA \cap TouchB = {}
InOrder == ~bad
AllLayersRan == lout = NB => \A i \in Layers : ld[i] = NB

\* A queue entry is not written while CPU1 still reads it.
EntryExclusive == apc \in {"run", "hand"} /\ bpc # "idle" => lin % LPQ # lout % LPQ

\* Every block handed over is finished: neither core waits for the other for ever.
AllFinished == <>(lout = NB)
====
