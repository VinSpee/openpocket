---- MODULE KnobHandoff ----
\* SPDX-License-Identifier: GPL-3.0-only
\* A change of settings handed from the main loop to the audio side.
\*
\* audio_apply (firmware/px_audio.h) gives a change of knobs or of blocks on / off to the audio side without
\* stopping the sound: it waits until the change before was taken (a_wait on a_kpend), prepares the cabinet's
\* filters in a store of its own (px_engine_prepare), copies the preset to a_kp and sets a_kpend. a_commit, at
\* the start of a block, applies it (px_engine_knobs reads a_kp and that store, writes the engine) and clears
\* a_kpend. a_commit runs in the audio interrupt on CPU0, and again in dsp_block, which CPU1 runs beside the
\* main loop. Any other change is a full one: the interrupt masked, a_collect, px_engine_set.
\*
\* Promise: a_kp, the cabinet store and the engine's preset are never read by one side while the other writes
\* them, and px_engine_set runs with no block in progress.
EXTENDS Naturals

CONSTANTS
  MaxApplies,    \* how many times the main loop calls audio_apply
  WaitForAck     \* TRUE: the code (a_wait(&a_kpend, 0, 50)); FALSE: a mutation that does not wait

VARIABLES
  mpc,      \* where the main loop is in audio_apply (a place it is in for a while, not an instant)
  kpend,    \* a_kpend
  masked,   \* the audio interrupt is masked
  job,      \* CPU1's block: "none", "start" (handed over), "knobs" (in px_engine_knobs), "rest" (the DSP)
  applies,  \* calls of audio_apply still to come
  clash     \* the interrupt applied a change while the main loop was reading or writing what it uses

vars == <<mpc, kpend, masked, job, applies, clash>>

\* the places where the main loop reads the engine's preset or writes what px_engine_knobs reads
Sharing == {"change", "prep", "copy", "fset"}

Init ==
  /\ mpc = "idle" /\ kpend = FALSE /\ masked = FALSE
  /\ job = "none" /\ applies = MaxApplies /\ clash = FALSE

\* ---- the main loop: audio_apply ----
Begin ==
  /\ mpc = "idle" /\ applies > 0
  /\ mpc' = "wait" /\ applies' = applies - 1
  /\ UNCHANGED <<kpend, masked, job, clash>>

\* a_wait: the change before was taken
WaitOk ==
  /\ mpc = "wait" /\ (~WaitForAck \/ ~kpend)
  /\ mpc' = "change"
  /\ UNCHANGED <<kpend, masked, job, applies, clash>>

\* a_wait: 50 ms passed and it was not: a full change instead
WaitTimeout ==
  /\ mpc = "wait" /\ WaitForAck /\ kpend
  /\ mpc' = "fmask"
  /\ UNCHANGED <<kpend, masked, job, applies, clash>>

\* px_engine_change has read the engine's preset: nothing to do, knobs only, or a full change
Change ==
  /\ mpc = "change"
  /\ mpc' \in {"idle", "prep", "fmask"}
  /\ UNCHANGED <<kpend, masked, job, applies, clash>>

\* px_engine_prepare has written the cabinet store
Prepare ==
  /\ mpc = "prep" /\ mpc' = "copy"
  /\ UNCHANGED <<kpend, masked, job, applies, clash>>

\* a_kp = pp is written
Copy ==
  /\ mpc = "copy" /\ mpc' = "flag"
  /\ UNCHANGED <<kpend, masked, job, applies, clash>>

\* a_kpend = 1
Flag ==
  /\ mpc = "flag" /\ mpc' = "idle" /\ kpend' = TRUE
  /\ UNCHANGED <<masked, job, applies, clash>>

\* the full change: fm1_irq_mask
FullMask ==
  /\ mpc = "fmask" /\ mpc' = "fcollect" /\ masked' = TRUE
  /\ UNCHANGED <<kpend, job, applies, clash>>

\* a_collect: CPU1's block is done
FullCollect ==
  /\ mpc = "fcollect" /\ job = "none"
  /\ mpc' = "fset"
  /\ UNCHANGED <<kpend, masked, job, applies, clash>>

\* px_engine_set has run
FullSet ==
  /\ mpc = "fset" /\ mpc' = "fclear"
  /\ UNCHANGED <<kpend, masked, job, applies, clash>>

\* a_kpend = 0: a change of knobs still waiting is part of the preset just set
FullClear ==
  /\ mpc = "fclear" /\ mpc' = "funmask" /\ kpend' = FALSE
  /\ UNCHANGED <<masked, job, applies, clash>>

\* fm1_irq_unmask
FullUnmask ==
  /\ mpc = "funmask" /\ mpc' = "idle" /\ masked' = FALSE
  /\ UNCHANGED <<kpend, job, applies, clash>>

MainStep ==
  \/ WaitOk \/ WaitTimeout \/ Change \/ Prepare \/ Copy \/ Flag
  \/ FullMask \/ FullCollect \/ FullSet \/ FullClear \/ FullUnmask

\* ---- the audio side ----
\* the audio interrupt (a_block): the block before collected, a_commit on CPU0 (the main loop stands still
\* meanwhile, wherever it is), the block handed to CPU1
Irq ==
  /\ ~masked /\ job = "none"
  /\ job' = "start"
  /\ kpend' = FALSE
  /\ clash' = (clash \/ (kpend /\ mpc \in Sharing))
  /\ UNCHANGED <<mpc, masked, applies>>

\* dsp_block on CPU1: a_commit reads a_kpend
Cpu1Check ==
  /\ job = "start"
  /\ job' = IF kpend THEN "knobs" ELSE "rest"
  /\ UNCHANGED <<mpc, kpend, masked, applies, clash>>

\* px_engine_knobs has run: a_kpend = 0
Cpu1Knobs ==
  /\ job = "knobs" /\ job' = "rest" /\ kpend' = FALSE
  /\ UNCHANGED <<mpc, masked, applies, clash>>

\* the block is made
Cpu1Done ==
  /\ job = "rest" /\ job' = "none"
  /\ UNCHANGED <<mpc, kpend, masked, applies, clash>>

Next == Begin \/ MainStep \/ Irq \/ Cpu1Check \/ Cpu1Knobs \/ Cpu1Done

Spec ==
  /\ Init /\ [][Next]_vars
  /\ WF_vars(MainStep) /\ WF_vars(Irq)
  /\ WF_vars(Cpu1Check) /\ WF_vars(Cpu1Knobs) /\ WF_vars(Cpu1Done)

TypeOK ==
  /\ mpc \in {"idle", "wait", "change", "prep", "copy", "flag", "fmask", "fcollect", "fset", "fclear", "funmask"}
  /\ kpend \in BOOLEAN /\ masked \in BOOLEAN
  /\ job \in {"none", "start", "knobs", "rest"}
  /\ applies \in 0..MaxApplies /\ clash \in BOOLEAN

\* The promise: px_engine_knobs never runs while the main loop reads the engine's preset or writes a_kp or the
\* cabinet store.
NoTornHandoff == ~clash /\ ~(job = "knobs" /\ mpc \in Sharing)

\* px_engine_set, and the a_kpend = 0 after it, run with no block in progress and the interrupt masked.
SetRunsAlone == mpc \in {"fset", "fclear"} => job = "none" /\ masked

\* A change handed over is applied, or taken over by a full change.
Taken == kpend ~> ~kpend
====
