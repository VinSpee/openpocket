/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — Milestone 1 (boot + screen) — Donner Pocket X.
 * SCREEN (reverse-engineered from the stock firmware): SOFTWARE SPI on PORT B
 *   CS = PB1, D/C = PB5, CLK = PB6, DO = PB7   (PC7 is not the screen's: the button lights' enable, px_leds.h)
 * (the FM-1 used port C: that was the cause of the black screen.)
 * Init sequence + 40-line offset: taken verbatim from the Pocket X firmware.
 * Safety net: 2 failed boots -> UBOOT (re-flashable), + backup fm1-1.bin.
 */
#include <stdint.h>
#define AUDIO_PROBE 0      /* 1 = audio test (find IN/OUT on the I2S) instead of the UI */
#define DSP_CPU1 1         /* 1 = DSP engine on the 2nd core (automatic fallback to CPU0 on trouble) */
#include "base/board.h"               /* the chip, the pins, the screen and its text (docs/BASE-FIRMWARE.md) */
/* ---- preset: the v2 engine's struct (engine/px_dsp.h), edited directly by the UI ---- */
#include "px_dsp.h"
#include "px_nam.h"           /* v37: NAM A2-Lite (the engine includes px_nam.c) */
#include "px_midi.h"                    /* MIDI map and settings (docs/MIDI.md) */
static px_preset_t PRESET;
static uint32_t cur_preset = 0;
static uint32_t master_vol = 80;        /* 0..100, follows the MASTER knob (ADC channel 2) */
static void st_get(uint32_t k, px_preset_t *p);   /* px_store.h: the saved preset of slot k, else the factory one */
static uint8_t preset_new;                             /* v40.3: a preset was loaded: the sound follows at once */
static void load_preset(uint32_t i) { st_get(i, &PRESET); preset_new = 1; }

#if AUDIO_PROBE
void fm1_timer5_irq(void) { }
#endif
#if !AUDIO_PROBE
void op_audio_irq(void) { }           /* internal DAC: not used by the Pocket X */
#endif
/* readable crash screen: vector, PC, return address, bus error register — then reboot */
static void hex8(char *b, uint32_t v){ static const char H[]="0123456789ABCDEF";
    for (int i=0;i<8;i++) b[i]=H[(v>>(28-4*i))&15u]; b[8]=0; }
static void fm1_fault(const fm1_crash_t *c)
{
    char h[9];
    lcd_rect(0,0,240,240,RED);
    lcd_text(8,8,"CRASH",WHT,RED,2);
    lcd_text(8,40,"VEC",WHT,RED,1); hex8(h,c->vec);  lcd_text(64,40,h,WHT,RED,1);
    lcd_text(8,56,"PC", WHT,RED,1); hex8(h,c->pc);   lcd_text(64,56,h,WHT,RED,1);
    lcd_text(8,72,"RETS",WHT,RED,1);hex8(h,c->rets); lcd_text(64,72,h,WHT,RED,1);
    lcd_text(8,88,"DBG",WHT,RED,1); hex8(h,c->dbg);  lcd_text(64,88,h,WHT,RED,1);
    lcd_text(8,104,"EMU",WHT,RED,1);hex8(h,c->emu);  lcd_text(64,104,h,WHT,RED,1);
    lcd_text(8,136,"Take a photo :)",WHT,RED,1);
    for (int i=0;i<6;i++){ fm1_wdt_feed(); fm1_delay_ms(1000); }
    fm1_reboot();
}

#ifndef OP_VERSION_NUM                  /* a test channel build (firmware/build.sh, OP_CHANNEL) sets its own number and tag */
#define OP_VERSION_NUM "42"
#endif
#ifndef OP_TAG
#define OP_TAG ""
#endif
#ifndef OP_GUARD_STACK      /* hardware stack-limit guard (fm1_guard.h). build.sh turns it off for the Bluetooth test image, */
#define OP_GUARD_STACK 1    /* whose task stacks (ble_os.c) lie outside [_ustack_lo, _sstack_top]; app.ld checks this at link time */
#endif
#define OP_VERSION "v" OP_VERSION_NUM OP_TAG   /* shown at boot: changes with every released build */
#define PROBE 0            /* 1 = GPIO diagnostic: shows the pins that change at the bottom */
#define ADC_PROBE 0        /* 1 = ADC diagnostic: shows the channels that move at the bottom (C2:512) */
#define LED_PROBE 0        /* 1 = LED map (issue #8, led_probe.h): the knob picks a bit of the lights' 595, it blinks; no sound */
#include "base/controls.h"            /* the controls' pins, the ADC, the battery, the debounce */
/* ===== boot diagnostics (v23b) : survive a reset in .noinit, shown at the next boot =====
 * stage = last boot step reached; blocks/load/peak/nans/c1/loops = audio state seen by the main loop. */
#define OPD_MAGIC 0x4F504442u
enum { ST_NONE, ST_INPUTS, ST_PRESET, ST_ENG_INIT, ST_ENG_SET, ST_I2S, ST_UI, ST_RUN, ST_OK, ST_N };
static const char *const ST_NAME[ST_N] = {"?", "INPUTS", "PRESET", "ENGINE INIT", "ENGINE SET", "I2S START",
                                          "UI DRAW", "FIRST 6 S", "OK"};
volatile struct { uint32_t magic, stage, blocks, load, peak, nans, c1, loops, crash_seen, safe; } op_diag
    __attribute__((section(".noinit")));
static struct { uint8_t vdc0, vdc1, sys0, sys1, dcdc, rst, pinr; } op_pwr;   /* supplies, reset source, pin reset (fm1_cstart) */
static void op_stage(uint32_t s, const char *txt)
{
    op_diag.stage = s;
    fm1_wdt_feed();
    (void)txt;
}

#if AUDIO_PROBE
#include "audio_probe.h"
#else
#include "px_cacheram.h"     /* v37: data-cache ways as RAM (melodee's routine): the NAM network's vector-unit buffers */
#include "px_audio.h"
#endif

#if !AUDIO_PROBE
#include "ui.h"
#include "px_lights.h"      /* the button lights follow the blocks (issue #8; px_leds.h drives them) */
#define PX_LIGHTS_OFF() lights_off()
#include "px_usb.h"         /* USB-MIDI + update entry (web installer) */
#include "px_store.h"       /* the user's presets in flash */
#include "px_models.h"      /* v37: the NAM user models in flash */
#include "px_irs.h"         /* v38: the user cabinet IRs in flash */
#if !AUDIO_PROBE
static uint32_t ed_load(void) { return a_bypass ? a_peak : a_load; }   /* PING: the DSP load, as at the bottom right */
static uint32_t ed_flags(void) { return (a_bypass ? 1u : 0u) | (a_c1 ? 2u : 0u); }
/* v40.1: the stacks' deepest use since boot. fm1_cstart paints them; the bytes at the bottom never written since are free */
#define OP_PAINT 0x5AA5C33Cu
extern char _ustack_top[];
static uint32_t stack_free(const char *lo, const char *hi)
{
    const uint32_t *p = (const uint32_t *)(const void *)lo;
    while ((const char *)p < hi && *p == OP_PAINT) p++;
    return (uint32_t)((const char *)p - lo);
}
#if OP_BLE_TEST
uint32_t ble_heap_min_free(void);
uint32_t ble_stack_min_free(void);
#endif
static uint32_t ed_stat(uint32_t k)                    /* PING, after the flags */
{
    return k == 0u ? a_peak : k == 1u ? a_peak_all : k == 2u ? a_xrun : k == 3u ? a_clip
         : k == 4u ? stack_free(_ustack_lo, _ustack_top) : k == 5u ? stack_free(_sstack_lo, _sstack_top)
#if OP_BLE_TEST
         : k == 6u ? ble_heap_min_free() : ble_stack_min_free();
#else
         : 0u;
#endif
}
/* v40.2: PING's profile (14 bits each): each block's and the NAM network's share of an audio block's time over the last
 * second (%, a_pf), the network's way of running (pxn_mode), then from its start-up timing (0.1 % of real time, the
 * network alone): each way of running (px_nam_tuned), and where one core's time goes (pxn_prof_ticks); then the layer
 * split: 0 off, else 1 + the last layer CPU0 runs; then with it CPU0's and CPU1's load and their peaks since the preset
 * was set (%); v40.3: then the last full change: from the change to the sound back (0.1 ms, 0 while it fades in), the
 * engine's set in it (0.1 ms), how long the last LOADING was shown (ms) and how many were since boot */
#define ED_PROF (PX_NBLK + 2u + PXN_MODES + 9u + 5u + 4u)
static uint32_t ed_prof(uint32_t k)
{
    if (k <= PX_NBLK) return a_pf[k];
    if (k == PX_NBLK + 1u) return (uint32_t)pxn_mode();
    k -= PX_NBLK + 2u;
    if (k == PXN_MODES + 9u) return g_eng.nam_lp ? 1u + (uint32_t)px_nam_lp_split_used() : 0u;   /* v40.2 (v40.4: the last block's) */
    if (k > PXN_MODES + 9u && k <= PXN_MODES + 13u) {  /* each core's load and peak (%), the layer split only */
        const uint32_t j = k - PXN_MODES - 10u;
        return g_eng.nam_lp ? (j < 2u ? a_cl[j] : a_cpk[j - 2u]) : 0u;
    }
    if (k >= PXN_MODES + 14u) {                        /* v40.3: the last full change (0.1 ms) and LOADING (ms, count) */
        const uint32_t d = FM1_TICKS_PER_US * 100u;
        const uint32_t v = k == PXN_MODES + 14u ? (a_swin ? 0u : (a_sw1 - a_swc) / d) : k == PXN_MODES + 15u ? a_swset / d
                         : k == PXN_MODES + 16u ? a_ldms : a_ldn;
        return v > 16383u ? 16383u : v;
    }
    const uint32_t t = k < PXN_MODES ? px_nam_tuned((int)k) : pxn_prof_ticks((int)(k - PXN_MODES));
    return t * 10u / (A_BUDGET * 2u / 100u);           /* (ticks for 256 frames: two audio blocks) */
}
static void nm_changed(uint32_t s)   /* v37: a user model was written or erased: the engine reloads it if it plays it */
{
    if (g_eng.nam_idx == (int)(PX_NAM_NFACT + s)) g_eng.nam_idx = -1;
    a_full = 1;                                        /* v38: the same knobs, another network: a full apply */
    audio_apply();
}
static void ir_changed(uint32_t s)   /* v38: a user IR was written or erased: the engine reloads it if it plays it */
{
    if (g_eng.ir_slot == (int)s) g_eng.ir_slot = -1;
    a_full = 1;
    audio_apply();
}
#if OP_BLE_TEST             /* the log channel (firmware/ble/ble_diag.h): only this image answers DIAG, the normal firmware does not change */
#define ED_STATS 8u         /* PING also gives the Bluetooth heap's and tasks' lowest free bytes */
#define ED_DIAG_LINES 5u
#define ED_DIAG_LEN 224u
static void ed_diag_line(uint32_t i, char *out);
#endif
#include "px_editor.h"      /* the web editor (docs/EDITOR-PROTOCOL.md) */
#include "px_midi_io.h"     /* musical MIDI over USB (docs/MIDI.md) */
#endif
#endif
#if LED_PROBE
#if AUDIO_PROBE
#error "LED_PROBE needs the normal build: USB (the web installer) is the way back"
#endif
#include "led_probe.h"      /* the LED map (issue #8) */
#endif
#if OP_BLE_TEST             /* the Bluetooth MIDI test image (docs/BLE-TEST.md): make ble */
#include "ble/ble_os.c"
#include "ble/ble_libc.c"
#include "ble/ble_cfg.c"
#include "ble/ble_rf.c"
#include "ble/ble_stubs.c"
#include "ble/ble_test.c"
#include "ble/ble_midi.c"
#include "ble/ble_diag.h"
static void ed_diag_line(uint32_t i, char *out)
{
    if (i == 0u) ble_boot_text(out, ms_now(), op_diag.safe, op_diag.stage, op_pwr.rst, guard.failed);
    else if (i == 1u) ble_last_text(out, &ble_last);
    else if (i == 2u) ble_test_line(out);
    else if (i == 3u) ble_midi_line(out);
    else ble_trace_line(out);
}
#endif

/* P3_RST_SRC as read at boot: why the chip last reset (fm1_sys.h) */
static void rst_names(char *l, uint32_t r)
{
    static const char *const N[7] = {"POWER-ON", "VDDIO LOW", "WATCHDOG", "VCM", "LONG PRESS", "1.2V", "SOFT"};
    int any = 0;
    for (int i = 0; i < 7; i++)
        if (r & (1u << i)) { if (any) b_str(l, "+"); b_str(l, N[i]); any = 1; }
    if (!any) b_str(l, "NONE");
}

static void app_main(void)
{
    uint32_t boots_ok = 0;
    int32_t accum = 0;
    uint8_t enc_last;
    enum { B_PUSH, B_FSUP, B_FSDN, B_FX, B_MOD, B_AMP, B_CAB, B_DLY, B_REV, NBTN };
    dbn_t btn[NBTN] = {{0,0,0}};
    dbn_t bhome = {0,0,0};
    uint32_t adc_tick = 0, t_knob = 0, t_tuner = 0;
    int32_t master_raw = -100;
#if !AUDIO_PROBE
    {   /* before the second core starts: the data-cache ways as RAM (through a pointer: it is RAM code) */
        void (*volatile f)(void) = pxc_init;
        f();
    }
#endif
#if DSP_CPU1
    fm1_cpu1_bar_build(403u);            /* a new build gives the 2nd core another chance (v40.4: its job and the blocks' loops changed) */
    fm1_cpu1_start();                    /* at boot, before audio and interrupts (Felucca) */
#endif
    lcd_init();
    /* boot screen with the version: shows which image is running */
    lcd_rect(0,0,240,240,UI_BG);
    ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
#if OP_BLE_TEST
    ft_box(0, 126, 240, OP_VERSION " BLE TEST", &FT_SM, UI_DIM, UI_BG, 1);
#else
    ft_box(0, 126, 240, OP_VERSION, &FT_SM, UI_DIM, UI_BG, 1);
#endif
#if !AUDIO_PROBE
    ft_box(0, 150, 240, "STARTING", &FT_SM, UI_TXT, UI_BG, 1);   /* v42: a plain word first, the numbers small below */
    {   /* the supplies as found (fm1_cstart) and why the chip last reset */
        char l[32]; l[0] = 0;
        b_str(l, "PWR "); b_num(l, op_pwr.vdc0); b_str(l, " "); b_num(l, op_pwr.sys0); b_str(l, " (BOOT LEVELS)");
        ft_box(0, 196, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
        char h[9]; l[0] = 0;
        b_str(l, "RST "); rst_names(l, op_pwr.rst); b_str(l, " PINR "); hex8(h, op_pwr.pinr); b_str(l, h + 6);
        ft_box(0, 214, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
    }
#endif
    for (int i = 0; i < 6; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
    /* --- report of the previous boot, if it did not finish (or crashed) --- */
    int safe = 0;
    {
        uint32_t cc = (fm1_crash.magic == FM1_CRASH_MAGIC) ? fm1_crash.count : 0u;
        if (op_diag.magic != OPD_MAGIC) {
            op_diag.magic = OPD_MAGIC; op_diag.stage = ST_OK; op_diag.crash_seen = 0; op_diag.safe = 0;   /* a crash left by an older build is shown once */
        }
        int crashed = cc != op_diag.crash_seen;
        uint32_t st = op_diag.stage < ST_N ? op_diag.stage : 0u;
        if ((st != ST_OK || crashed) && !op_diag.safe) {
            char l[48], h[9];
            safe = 1;                    /* this boot: DSP bypassed, so the pedal stays usable */
            lcd_rect(0, 0, 240, 240, UI_BG);   /* v42: what happened and what to do first, the numbers for a bug report below */
            ft_box(0, 6, 240, "LAST START FAILED", &FT_MD, RED, UI_BG, 1);
            ft_box(0, 34, 240, "THE EFFECTS ARE OFF FOR THIS START", &FT_SM, UI_TXT, UI_BG, 1);
            ft_box(0, 52, 240, "SWITCH OFF AND ON TO TRY AGAIN", &FT_SM, UI_TXT, UI_BG, 1);
            l[0] = 0; b_str(l, "STOPPED AT: "); b_str(l, ST_NAME[st]);
            ft_box(0, 78, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            if (!crashed) {
                l[0] = 0; b_str(l, "RESET BY: "); rst_names(l, op_pwr.rst);
                ft_box(0, 96, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            }
            if (crashed) {
                l[0] = 0; b_str(l, "CRASH  PC "); hex8(h, fm1_crash.pc); b_str(l, h);
                ft_box(0, 96, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
                l[0] = 0; b_str(l, "RETS "); hex8(h, fm1_crash.rets); b_str(l, h); b_str(l, "  VEC "); b_num(l, fm1_crash.vec);
                ft_box(0, 114, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
                l[0] = 0; b_str(l, "DSP CODE AT "); hex8(h, (uint32_t)(uintptr_t)px_block_label); b_str(l, h);
                ft_box(0, 132, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
                l[0] = 0; b_str(l, "UPTIME "); b_num(l, fm1_crash.uptime_ms); b_str(l, " MS  DBG "); hex8(h, fm1_crash.dbg); b_str(l, h);
                ft_box(0, 150, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            }
            l[0] = 0; b_str(l, "BLOCKS "); b_num(l, op_diag.blocks); b_str(l, "  LOAD "); b_num(l, op_diag.load);
            b_str(l, "%  PEAK "); b_num(l, op_diag.peak); b_str(l, "%");
            ft_box(0, 168, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            l[0] = 0; b_str(l, "NAN "); b_num(l, op_diag.nans); b_str(l, "  CPU1 "); b_num(l, op_diag.c1);
            b_str(l, "  LOOPS "); b_num(l, op_diag.loops);
            ft_box(0, 186, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            ft_box(0, 212, 240, "PHOTO THIS FOR A BUG REPORT", &FT_SM, UI_ACC, UI_BG, 1);
            for (int i = 0; i < 120; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }   /* 12 s */
            lcd_rect(0, 0, 240, 240, UI_BG);
            ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
            ft_box(0, 126, 240, OP_VERSION " SAFE MODE", &FT_SM, UI_DIM, UI_BG, 1);
        }
#if OP_BLE_TEST
        ble_last = (ble_boot_t){ st != ST_OK || crashed, crashed, st, op_diag.blocks, op_diag.load, op_diag.peak, op_diag.nans, op_diag.c1,
                                 op_diag.loops, fm1_crash.vec, fm1_crash.pc, fm1_crash.rets, fm1_crash.dbg, fm1_crash.emu, fm1_crash.uptime_ms };
#endif
        op_diag.crash_seen = cc; op_diag.safe = (uint32_t)safe;
        op_diag.blocks = op_diag.load = op_diag.peak = op_diag.nans = op_diag.c1 = op_diag.loops = 0;
    }
    op_stage(ST_INPUTS, "INPUTS...");
    input_init();
    adc_pins_analog();
    input_baseline();
    keys_capture_idle();
    op_stage(ST_PRESET, "PRESET...");
    load_preset(cur_preset);
#if LED_PROBE
    px_flash_boot();
    px_store_boot();
    px_usb_start();                      /* USB-MIDI + update entry: the web installer is the way back */
    fm1_irq_enable_all();                /* audio_start does it in the normal build; here no audio is started */
    led_probe_run();                     /* never returns */
#endif
#if AUDIO_PROBE
    audio_probe_run();
#else
    a_hold = 1;                          /* DSP bypassed until the UI is up and 1.5 s have passed */
    px_flash_boot();                     /* the flash's size; a staging area an update left */
    px_store_boot();                     /* the saved presets and the settings; start on the last one saved */
    px_usb_start();                      /* USB-MIDI device + update entry (web installer); v42: USB audio when set */
    nm_scan();                           /* v37: the NAM user models (names, levels) */
    ir_scan();                           /* v38: the user cabinet IRs (names) */
#if DSP_CPU1
    px_nam_helper(a_hrun, a_hwait);      /* v37: CPU1 can take a part of the NAM network */
#endif
#if OP_BLE_TEST
    {   /* the vector unit's inputs: what the cache RAM has left after the tuner's buffers (app.ld .cram) */
        extern char _cram_hi[];
        uint32_t lo = (uint32_t)(uintptr_t)_cram_hi;
        if (pxc_status == PXC_READY && lo < PXC_END) px_nam_fast((void *)(uintptr_t)lo, PXC_END - lo);
        if (pxc_status == PXC_READY && lo + PXN_FAST_BYTES + PX_NAM_LP_BYTES <= PXC_END)   /* v40.2: then the layer split's */
            px_nam_lp_mem((void *)(uintptr_t)(lo + PXN_FAST_BYTES), PX_NAM_LP_BYTES);
    }
#else
    if (pxc_status == PXC_READY) {
        px_nam_fast((void *)PXC_BEGIN, PXC_END - PXC_BEGIN);   /* the vector unit's inputs */
        px_nam_lp_mem((void *)(PXC_BEGIN + PXN_FAST_BYTES), PX_NAM_LP_BYTES);   /* v40.2: then the layer split's queue */
    }
#endif
    /* v37: the NAM routines by hand checked, its fastest way to run chosen. v40.2: not after a failed boot (safe mode,
     * the DSP off anyway): a hand routine that faults in its check then costs one boot, not every one, and the pedal
     * comes up with USB-MIDI for the installer's rescue (it ran C until the next boot) */
    if (!safe) px_nam_check(b_ticks);
    /* v40.5: from here (after the start-up check), CPU0's waits for CPU1 inside the network end as fm1_cpu1_wait's
     * do (px_audio.h dsp_lp_run: retired, LATE) */
    px_nam_lp_clock(b_ticks, FM1_CPU1_TIMEOUT_US * FM1_TICKS_PER_US);
    audio_start();                       /* guitar -> DSP engine -> output, amp enabled */
    leds_init();                         /* the button lights: all off loaded, then on (PC7 low), as stock */
    op_stage(ST_UI, "SCREEN...");
    lcd_rect(0, 0, 240, 240, UI_BG);
    ui_invalidate();
    ui_render();
    op_stage(ST_RUN, 0);
#endif
#if OP_BLE_TEST
    ble_os_init();                       /* the task table, before the main loop polls it (ble_os.c: it lives in NOINIT) */
    if (!safe)                           /* after a failed boot Bluetooth stays off: the pedal runs, and can be re-flashed */
        ble_test_start();                /* radio on, controller and host up, advertising (the pedal keeps working) */
#endif
    enc_last = (uint8_t)(PC_IN & ENC_MASK);
    for (;;) {
        uint32_t now = ms_now();
        fm1_wdt_feed();
#if OP_BLE_TEST
        ble_test_poll();
#endif
#if !AUDIO_PROBE
        px_usb_service(now);             /* web installer: handshake, update, UBOOT soft key */
        px_midi_service();               /* USB-MIDI: Program / Control Change in, the pedal's own changes out */
        lights_update(&PRESET, ui_mode == M_TUNER, now);   /* the button lights follow the blocks */
        if (ui_mode != M_TUNER) {        /* DSP load in the status corner: "DSP 42%", "PEAK 150%" (bypassed), "SAFE 42%" */
            static uint32_t t_cpu;
            static uint8_t ld_shown;
            if (now - t_cpu >= 500u || a_ld != ld_shown) {   /* (LOADING on and off at once) */
                t_cpu = now; ld_shown = a_ld;
                mid_c1_off = a_c1 ? 0u : (fm1_cpu1_gave_up ? fm1_cpu1_gave_up : 0xFFu);   /* v41: why the 2nd core stopped (MIDI page) */
                char c[16];
                int late = a_xrun && a_blocks - a_xrun_at < 700u;   /* a block ran late in the last 2 s: "DSP 88%!" in red */
                uint32_t ld = a_bypass ? a_peak : a_load;
                ui_dg.load = ld; ui_dg.peak = a_peak; ui_dg.cl[0] = a_cl[0]; ui_dg.cl[1] = a_cl[1];   /* (v42: the diagnostics page) */
                ui_dg.cores = (uint8_t)(a_c1 ? (px_nam_dual(&g_eng) ? 2u : 1u) : 0u);
                ui_dg.split = (uint8_t)(g_eng.nam_lp != 0); ui_dg.guard = (uint8_t)(a_c1 ? 93u : 85u);   /* (px_audio.h's dsp_load limits) */
                ui_dg.late = (uint8_t)late; ui_dg.bypass = a_bypass;
                ui_load_text(c, ld, safe, a_bypass, late);
                int kind = ui_load_kind(ld, safe, a_bypass, late, a_ld);   /* v40.3: LOADING a NAM model; v42: amber when high */
                bat_raw[2] = (int32_t)a_xrun; bat_raw[3] = (int32_t)a_clip; bat_raw[4] = (int32_t)a_peak_all;   /* diagnostics page */
#if OP_BLE_TEST
                {   /* "STARTING 4 C1 42%": the Bluetooth state first; FAILED and ADVERTISING scroll their long lines instead (ble_status.h) */
                    char b[48], full[128]; int k, j;
                    static uint32_t scroll;
                    ble_test_state(b);
                    if (ble_state == BLE_FAILED || ble_state == BLE_ADV) {   /* longer than the status area: scroll it (ble_status.h) */
                        if (ble_conn) ble_midi_line(full); else ble_test_line(full);
                        ble_marquee(full, scroll++, 11, b);
                    } else {
                        for (k = 0; b[k]; k++) ;
                        b[k++] = ' ';
                        for (j = 0; c[j]; j++) b[k++] = c[j];
                        b[k] = 0;
                    }
                    ui_status(b, kind);
                }
#else
                ui_status(c, kind);
#endif
            }
        }
        if ((ui_mode == M_TUNER || ui_mode == M_DIAG) && now - t_tuner >= 100u) {
            t_tuner = now;
            if (ui_mode == M_TUNER) tuner_draw(tuner_estimate()); else diag_draw();
        }
#endif
        if (!boots_ok && now > 6000u) {   /* 4.5 s of DSP first: a pedal that resets under load is caught */ boots_ok = 1; guard.pending = 0; guard.failed = 0; op_diag.stage = ST_OK; }
#if !AUDIO_PROBE
        if (a_hold && !safe && now > 1500u) a_hold = 0;   /* DSP on */
        {
            static uint32_t t_diag;
            op_diag.loops++;
            if (now - t_diag >= 100u) {
                t_diag = now;
                op_diag.blocks = a_blocks; op_diag.load = a_load; op_diag.peak = a_peak;
                op_diag.nans = a_nan_count; op_diag.c1 = a_c1;
            }
        }
#endif

        /* --- encoder (direction inverted: subtract); fast rotation = steps of 5 --- */
        uint8_t s = (uint8_t)(PC_IN & ENC_MASK);
        if (s != enc_last) {
            accum -= QDEC[(enc_last << 2) | s];
            enc_last = s;
            int dd = 0;
            if (accum >= 4) { accum -= 4; dd = 1; } else if (accum <= -4) { accum += 4; dd = -1; }
            if (dd) {
                int fast = (now - t_knob) < 45u; t_knob = now;
                ui_turn(dd, fast);
            }
        }
        /* --- buttons (pressed = differs from the idle state) --- */
        uint32_t pa = (PA_IN & 0xFFFFu) ^ base_pa, ph = (PH_IN & 0xFFFFu) ^ base_ph;
        /* debounced: what is held. ui_buttons (ui.h) times the gestures: PUSH, HOME and FX act at a short release or after a
         * hold, the others at the press */
        dbn_press(&btn[B_PUSH], (ph & PH_PUSH) != 0);
        dbn_press(&btn[B_FSUP], (pa & PA_FS_UP) != 0);
        dbn_press(&btn[B_FSDN], (pa & PA_FS_DN) != 0);
        dbn_press(&btn[B_FX], (pa & PA_FX) != 0);
        dbn_press(&btn[B_MOD], (ph & PH_MOD) != 0);
        dbn_press(&btn[B_AMP], (ph & PH_AMP) != 0);
        dbn_press(&bhome, (pa & PA_HOME) != 0);

        int32_t mv = -1;
        if (now - adc_tick >= 10u) {     /* analog keypad CAB/DLY/REV + MASTER knob, every 10 ms */
            adc_tick = now;
            {   /* battery: every 2 s (v36) */
                static uint32_t t_bat;
                if (!t_bat || now - t_bat >= 2000u) { t_bat = now ? now : 1u; bat_read(); }
            }
            int k = keys_decode(adc_read2(ADC_CH_KEYS));
            dbn_press(&btn[B_CAB], k == KEY_CAB);
            dbn_press(&btn[B_DLY], k == KEY_DLY);
            dbn_press(&btn[B_REV], k == KEY_REV);
            mv = adc_read2(ADC_CH_MASTER);
        }
#if !AUDIO_PROBE
        ui_buttons(now, (uint32_t)(btn[B_FSUP].stab != 0) << UB_FSUP | (uint32_t)(btn[B_FSDN].stab != 0) << UB_FSDN |
                        (uint32_t)(btn[B_PUSH].stab != 0) << UB_PUSH | (uint32_t)(bhome.stab != 0) << UB_HOME |
                        (uint32_t)(btn[B_FX].stab != 0) << UB_FX | (uint32_t)(btn[B_MOD].stab != 0) << UB_MOD |
                        (uint32_t)(btn[B_AMP].stab != 0) << UB_AMP | (uint32_t)(btn[B_CAB].stab != 0) << UB_CAB |
                        (uint32_t)(btn[B_DLY].stab != 0) << UB_DLY | (uint32_t)(btn[B_REV].stab != 0) << UB_REV);
#endif
        if (mv >= 0) {
            /* smoothing (x16 moving average, ~160 ms) then hysteresis of 2 on the 0..100 volume */
            master_raw = (master_raw < 0) ? mv * 16 : master_raw + mv - master_raw / 16;
            int32_t avg = master_raw / 16;
            uint32_t nv;
            if (avg <= 12) nv = 0u;
            else if (avg >= 1010) nv = 100u;
            else nv = (uint32_t)(avg - 12) * 100u / (1010u - 12u);
            int32_t dv = (int32_t)nv - (int32_t)master_vol; if (dv < 0) dv = -dv;
            if (dv >= 2 || (nv == 0u && master_vol != 0u) || (nv == 100u && master_vol != 100u)) master_vol = nv;
        }
#if !AUDIO_PROBE
        audio_poll(now, preset_new);     /* the sound follows the UI (preset, settings, volume), before the screen redraws */
        preset_new = 0;
        ui_render();                     /* redraws only what changed */
#endif
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    uint32_t vdc0, sys0, dcdc, rst0, pinr0;
    {   /* v40.1: paint the stacks (interrupts are still off), for their deepest use (ed_stat) */
        volatile uint32_t here;
        for (d = (uint32_t *)(void *)_ustack_lo; (char *)d < (char *)&here - 256; d++) *d = OP_PAINT;
        for (d = (uint32_t *)(void *)_sstack_lo; (char *)d < _sstack_top - 256; d++) *d = OP_PAINT;
    }
    fm1_time_init();
    /* v31, issue #1: the stock firmware's first act is P3_PINR_CON = 0, the long-press reset off (BlackBox_020
     * f_0200698a). Left on, a pin held at its active level resets the chip every 4 s (the SDK offers 4 or 8 s):
     * Megal's BlackBox reset every 4.3 s after a power cycle, never after an install (the always-on P33 kept the
     * stock firmware's 0 across the warm reset). Read first, so the boot screen can show it. */
    rst0 = fm1_p33_read(FM1_P3_RST_SRC);
    pinr0 = fm1_p33_read(0x94u);
    fm1_p33_write(0x94u, 0);
    fm1_wdt_arm(0x0D);
    /* v33: the supplies stay as the boot loader leaves them (VDC14 3, SYSVDD 11, LDO), as up to v29. v30 set them as
     * the stock firmware does (VDC14 7, SYSVDD 15, the 1.3 V supply on the DC-DC): it was not what fixed issue #1
     * (the long-press reset was), and with it a Pocket X ran its DSP ~25 % slower (DOOM SLUDGE 61 % -> 77 %) and
     * crackled. Only read, for the boot screen. */
    vdc0 = fm1_vdc14();
    sys0 = fm1_sysvdd();
    dcdc = 0;
    fm1_wdt_feed();
    if (guard.magic != 0x48575431u || guard.pending > 1u || guard.failed > 1u) {
        guard.magic = 0x48575431u; guard.failed = 0; guard.pending = 0;
    }
    if (guard.pending) guard.failed++;
    guard.pending = 1;
    if (guard.failed >= 2u) { guard.failed = 0; guard.pending = 0; fm1_enter_uboot(); }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (d = _bss_hi_start; d < _bss_hi_end; d++) *d = 0;
    for (d = _pool_tail_start; d < _pool_tail_end; d++) *d = 0;
    for (d = _bss_lo_start; d < _bss_lo_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    op_pwr.vdc0 = (uint8_t)vdc0; op_pwr.vdc1 = (uint8_t)fm1_vdc14();
    op_pwr.sys0 = (uint8_t)sys0; op_pwr.sys1 = (uint8_t)fm1_sysvdd(); op_pwr.dcdc = (uint8_t)dcdc;
    op_pwr.rst = (uint8_t)rst0; op_pwr.pinr = (uint8_t)pinr0;
    fm1_guard_enable((OP_GUARD_STACK ? FM1_GUARD_STACK : 0) | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;) ;
}
