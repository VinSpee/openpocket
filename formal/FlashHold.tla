---- MODULE FlashHold ----
\* SPDX-License-Identifier: GPL-3.0-only
\* The flash and the second core.
\*
\* The app runs from the flash through the SFC (XIP). fl_enter (firmware/bsp/chip/fm1_flash.h) switches the SFC
\* off for every erase, program and SPI read, with CPU0's interrupts off. CPU1 has no such switch: its idle loop is
\* in RAM, a job it runs is not all in RAM, so the rule is "CPU0 turns the flash off only with no job outstanding"
\* (firmware/bsp/chip/fm1_cpu1.h). st_audio_hold (firmware/px_store.h) keeps the rule for a write: the audio
\* interrupt masked, then a_collect.
\*
\* Promise: while the SFC is off, CPU1 is in its RAM loop.
\*
\* The main loop's steps are those of a NAM model commit from the editor (firmware/px_editor.h ED_NAM_COMMIT):
\* nm_commit (firmware/px_models.h), then nm_changed -> audio_apply (firmware/openpocket.c, firmware/px_audio.h).
\* ir_commit and ir_erase (firmware/px_irs.h) and nm_erase have the same shape.
EXTENDS Naturals, Sequences

CONSTANTS
  Modes,           \* how CPU1 is used in the runs explored: "oneshot" (dsp_job: one block a job) and / or
                   \* "worker" (lp_job: the NAM layer split, one job for as long as the preset plays)
  ScanInsideHold,  \* TRUE: the code (nm_scan before st_audio_hold(0)); FALSE: the scan after it, the order before
                   \* the fix of F1 (formal/FINDINGS.md)
  Collect,         \* TRUE: the code (a hold calls a_collect); FALSE: a mutation that leaves it out
  Gap              \* TRUE: the main loop may run between the interrupt's two callbacks. firmware/px_aorder.h:
                   \* a_enc runs, and the interrupt enters again for a_dec, a few microseconds later

VARIABLES
  mode,    \* the mode of this run
  mi,      \* the main loop's place in Program
  msub,    \* inside a step: "go" (not begun), "wait" (a_collect waits for CPU1), "off" (the SFC is off)
  masked,  \* the audio interrupt is masked (fm1_irq_mask(FM1_IRQ_ALNK0))
  cli,     \* CPU0's interrupts are off (irq_save)
  sfc,     \* "on": the flash is readable through XIP; "off": fl_enter switched the SFC off
  pend,    \* the audio interrupt is pending for a guitar block (a_enc)
  dpend,   \* it is pending for the output block of the guitar block just taken (a_dec)
  isr,     \* the interrupt: "none", "enc" (a_enc -> a_block), "dec" (a_dec -> a_send)
  cpu1,    \* "idle" (its RAM loop), "job" (dsp_job), "worker" (lp_job)
  ajob,    \* a_job (and a_lpw: the worker runs)
  served,  \* a_lpdone = a_lpreq: the worker has made the output asked for
  quit     \* a_lpquit

vars == <<mode, mi, msub, masked, cli, sfc, pend, dpend, isr, cpu1, ajob, served, quit>>

\* st_audio_hold(1) and audio_apply's full change start alike
Hold == <<"mask", "collect">>

\* nm_commit: the hold, the erases, the programs and the read back, then nm_scan (16 SPI reads) and the
\* release; nm_changed -> audio_apply: the hold, px_engine_set (it reads the model's weights over SPI), the
\* release. "write", "scan" and "load" are each one flash access or more.
Program ==
  Hold \o <<"write">> \o
  (IF ScanInsideHold THEN <<"scan", "unmask">> ELSE <<"unmask", "scan">>) \o
  Hold \o <<"load", "unmask">>

Step == IF mi <= Len(Program) THEN Program[mi] ELSE "done"
FlashSteps == {"write", "scan", "load"}

Init ==
  /\ mode \in Modes
  /\ mi = 1 /\ msub = "go"
  /\ masked = FALSE /\ cli = FALSE /\ sfc = "on"
  /\ pend = FALSE /\ dpend = FALSE /\ isr = "none"
  /\ cpu1 = "idle" /\ ajob = FALSE /\ served = TRUE /\ quit = FALSE

\* ---- the main loop (CPU0). It does not run while the interrupt does. ----
MainRuns == isr = "none"
Advance == mi' = mi + 1 /\ msub' = "go"

\* fm1_irq_mask(FM1_IRQ_ALNK0)
Mask ==
  /\ MainRuns /\ Step = "mask"
  /\ masked' = TRUE /\ Advance
  /\ UNCHANGED <<mode, cli, sfc, pend, dpend, isr, cpu1, ajob, served, quit>>

\* fm1_irq_unmask(FM1_IRQ_ALNK0): st_audio_hold(0), or the end of audio_apply
Unmask ==
  /\ MainRuns /\ Step = "unmask"
  /\ masked' = FALSE /\ Advance
  /\ UNCHANGED <<mode, cli, sfc, pend, dpend, isr, cpu1, ajob, served, quit>>

\* a_collect, called by the main loop: a_job cleared, the worker asked to return, then fm1_cpu1_wait
CollectBegin ==
  /\ MainRuns /\ Step = "collect" /\ msub = "go"
  /\ IF Collect /\ ajob
       THEN /\ ajob' = FALSE /\ quit' = (mode = "worker")
            /\ msub' = "wait" /\ mi' = mi
       ELSE /\ Advance /\ UNCHANGED <<ajob, quit>>
  /\ UNCHANGED <<mode, masked, cli, sfc, pend, dpend, isr, cpu1, served>>

\* fm1_cpu1_wait returned: the job is done, or the worker has returned (a_lpdone = a_lpreq)
CollectEnd ==
  /\ MainRuns /\ Step = "collect" /\ msub = "wait"
  /\ cpu1 = "idle"
  /\ served' = TRUE /\ Advance
  /\ UNCHANGED <<mode, masked, cli, sfc, pend, dpend, isr, cpu1, ajob, quit>>

\* fl_erase4k, fl_write or ota_fread: irq_save, then fl_enter switches the SFC off
FlashOff ==
  /\ MainRuns /\ Step \in FlashSteps /\ msub = "go"
  /\ cli' = TRUE /\ sfc' = "off"
  /\ msub' = "off" /\ mi' = mi
  /\ UNCHANGED <<mode, masked, pend, dpend, isr, cpu1, ajob, served, quit>>

\* fl_exit switches the SFC back on, then irq_restore; more: another access of the same step follows
FlashOnThen(more) ==
  /\ Step \in FlashSteps /\ msub = "off"
  /\ sfc' = "on" /\ cli' = FALSE
  /\ IF more THEN msub' = "go" /\ mi' = mi ELSE Advance
  /\ UNCHANGED <<mode, masked, pend, dpend, isr, cpu1, ajob, served, quit>>
FlashOn == \E more \in BOOLEAN : FlashOnThen(more)

Main == Mask \/ Unmask \/ CollectBegin \/ CollectEnd \/ FlashOff \/ FlashOn

\* ---- the audio interrupt (CPU0, firmware/px_aorder.h and firmware/px_audio.h) ----
\* the I2S has a guitar block: it comes every 2.9 ms, masked or not
BlockDue ==
  /\ ~pend /\ pend' = TRUE
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, dpend, isr, cpu1, ajob, served, quit>>

\* the interrupt enters for the guitar block (the output block of the one before goes first)
EncEnter ==
  /\ pend /\ ~dpend /\ ~masked /\ ~cli /\ isr = "none"
  /\ isr' = "enc" /\ pend' = FALSE
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, dpend, cpu1, ajob, served, quit>>

\* a_enc -> a_block: the job before collected (a_collect; the worker: a_lp_post, only its output), the next
\* one started (fm1_cpu1_run(dsp_job); the worker: a_lpreq++, and lp_job started if it does not run)
EncRun ==
  /\ isr = "enc"
  /\ IF mode = "oneshot"
       THEN /\ cpu1 = "idle"
            /\ cpu1' = "job" /\ ajob' = TRUE
            /\ UNCHANGED <<served, quit>>
       ELSE /\ ajob => served
            /\ ~ajob => cpu1 = "idle"
            /\ served' = FALSE
            /\ IF ajob THEN UNCHANGED <<cpu1, ajob, quit>>
                       ELSE cpu1' = "worker" /\ ajob' = TRUE /\ quit' = FALSE
  /\ IF Gap THEN isr' = "none" /\ dpend' = TRUE
            ELSE isr' = "dec" /\ dpend' = FALSE
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, pend>>

\* the interrupt enters again for the output block
DecEnter ==
  /\ dpend /\ ~masked /\ ~cli /\ isr = "none"
  /\ isr' = "dec" /\ dpend' = FALSE
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, pend, cpu1, ajob, served, quit>>

\* a_dec -> a_send: the block must be whole (a_collect; the worker: a_lp_post, and the worker goes on)
DecRun ==
  /\ isr = "dec"
  /\ IF mode = "oneshot"
       THEN /\ ajob => cpu1 = "idle"
            /\ ajob' = FALSE
       ELSE /\ ajob => served
            /\ UNCHANGED ajob
  /\ isr' = "none"
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, pend, dpend, cpu1, served, quit>>

\* ---- CPU1 (firmware/bsp/chip/fm1_cpu1.h fm1_cpu1_main, firmware/px_audio.h dsp_job and lp_job) ----
\* dsp_job returns
JobDone ==
  /\ cpu1 = "job" /\ cpu1' = "idle"
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, pend, dpend, isr, ajob, served, quit>>

\* lp_job: a request first (post_run, a_lpdone = ++done)
WorkerServe ==
  /\ cpu1 = "worker" /\ ~served /\ served' = TRUE
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, pend, dpend, isr, cpu1, ajob, quit>>

\* lp_job: asked to return (a_lpquit), and no request left
WorkerQuit ==
  /\ cpu1 = "worker" /\ served /\ quit /\ cpu1' = "idle"
  /\ UNCHANGED <<mode, mi, msub, masked, cli, sfc, pend, dpend, isr, ajob, served, quit>>

Next ==
  \/ Main
  \/ BlockDue \/ EncEnter \/ EncRun \/ DecEnter \/ DecRun
  \/ JobDone \/ WorkerServe \/ WorkerQuit

\* The main loop gets time between interrupts (SF: an interrupt may hold it off any number of times) and each
\* step's flash accesses end; the interrupt's callbacks and CPU1's work end.
Spec ==
  /\ Init /\ [][Next]_vars
  /\ SF_vars(Main) /\ SF_vars(FlashOnThen(FALSE))
  /\ WF_vars(EncRun) /\ WF_vars(DecEnter) /\ WF_vars(DecRun)
  /\ WF_vars(JobDone) /\ WF_vars(WorkerServe) /\ WF_vars(WorkerQuit)

TypeOK ==
  /\ mode \in {"oneshot", "worker"}
  /\ mi \in 1..(Len(Program) + 1)
  /\ msub \in {"go", "wait", "off"}
  /\ masked \in BOOLEAN /\ cli \in BOOLEAN /\ sfc \in {"on", "off"}
  /\ pend \in BOOLEAN /\ dpend \in BOOLEAN /\ isr \in {"none", "enc", "dec"}
  /\ cpu1 \in {"idle", "job", "worker"}
  /\ ajob \in BOOLEAN /\ served \in BOOLEAN /\ quit \in BOOLEAN

\* The promise: the SFC is off only while CPU1 is in its RAM loop.
XipExclusion == sfc = "off" => cpu1 = "idle"

\* The interrupt never runs with CPU0's interrupts off (a check of the model itself).
IsrNeedsIrqOn == cli => isr = "none"

\* The commit ends: a_collect never waits for ever.
Finishes == <>(Step = "done")
====
