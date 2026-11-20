/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — host test of the audio hold around the flash (firmware/px_store.h st_audio_hold): once the audio runs,
 * every erase, program and SPI read of a preset save and of a NAM model's or an IR's save and erase must fall between
 * the hold's a_collect and its release, because the flash switches the SFC off under the second core
 * (formal/FINDINGS.md, F1). It runs firmware/px_store.h, px_models.h and px_irs.h on a simulated NOR flash, as
 * tests/test_store.c does.
 *   cc -O1 -Iengine tests/test_flash_hold.c engine/px_dsp.c firmware/px_math.c -lm -o build/test_flash_hold && build/test_flash_hold */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "px_nam.h"
#include "../firmware/px_midi.h"

static uint8_t nor[0x100000];
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } } while (0)
static int running;                                /* the audio runs: after the start-up reads */
static int masked, collected;                      /* the hold: the audio interrupt masked, then a_collect */
static int held_n, loose_n;                        /* flash accesses inside the hold, and outside it */
static void flash(void) { if (running) { if (masked && collected) held_n++; else loose_n++; } }

/* ---- the firmware pieces the three files use ---- */
static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t c)
{
    while (n--) { c ^= (uint32_t)*p++ << 8; for (int i = 0; i < 8; i++) c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1; c &= 0xFFFFu; }
    return c;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { flash(); memcpy(p, nor + off, n); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *t) { (void)t; flash(); memset(nor + off, 0xFF, 0x1000); return 0; }
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    flash();
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static volatile uint8_t a_mute;
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(int ms) { (void)ms; }
#define FM1_IRQ_ALNK0 11
static void fm1_irq_mask(int n) { (void)n; masked = 1; }
static void fm1_irq_unmask(int n) { (void)n; masked = collected = 0; }
static void a_collect(void) { collected = masked; }
static uint8_t px_flash_ok = 1;
static uint32_t cur_preset;
static px_preset_t PRESET;
static void st_get(uint32_t k, px_preset_t *p);
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
#define ST_IRQ_OFF() ((void)0)
#include "../firmware/px_store.h"
#include "../firmware/px_models.h"
#include "../firmware/px_irs.h"

/* after one save or erase: it took the flash, only inside the hold, and gave the audio back */
static void held(const char *what)
{
    char msg[96];
    snprintf(msg, sizeof msg, "%s: %d of its %d flash accesses are outside the audio hold", what, loose_n, held_n + loose_n);
    CHECK(held_n > 0 && loose_n == 0, msg);
    snprintf(msg, sizeof msg, "%s: the audio is released at its end", what);
    CHECK(!masked && !a_mute, msg);
    held_n = loose_n = 0;
}

int main(void)
{
    char name[12];
    float level;
    memset(nor, 0xFF, sizeof nor);
    px_store_boot();
    nm_scan();
    ir_scan();
    running = 1;

    CHECK(st_save(3, &PRESET) == 0, "a preset save");
    held("st_save");

    {   /* a model the commit takes: its header, finite weights, their CRC */
        nm_hdr_t *h = (nm_hdr_t *)nm_stage;
        memset(nm_stage, 0, sizeof nm_stage);
        h->magic = NM_MAGIC; h->ver = 1; h->nw = PXN_NW;
        memcpy(h->name, "HOLD NAM", 9);
        h->crc = (uint16_t)ota_crc16(nm_stage + NM_HDR, PXN_NW * 4u, 0);
    }
    CHECK(nm_commit(2) == 0, "a NAM model's save");
    held("nm_commit");
    CHECK(px_nam_user(2, name, &level, 0) && !strcmp(name, "HOLD NAM"), "nm_commit: the slot's name is read back");
    CHECK(nm_erase(2) == 0, "a NAM model's erase");
    held("nm_erase");
    CHECK(!px_nam_user(2, name, &level, 0), "nm_erase: the slot reads as empty");

    {   /* an IR the commit takes */
        ir_hdr_t *h = (ir_hdr_t *)ir_stage;
        memset(ir_stage, 0, IR_BYTES);
        h->magic = IR_MAGIC; h->ver = 1; h->n = PX_IR_LEN; h->scale = 1.0f;
        memcpy(h->name, "HOLD IR", 8);
        h->crc = (uint16_t)ota_crc16(ir_stage + IR_HDR, PX_IR_LEN * 2u, 0);
    }
    CHECK(ir_commit(1) == 0, "an IR's save");
    held("ir_commit");
    CHECK(px_ir_user(1, name, 0) && !strcmp(name, "HOLD IR"), "ir_commit: the slot's name is read back");
    CHECK(ir_erase(1) == 0, "an IR's erase");
    held("ir_erase");
    CHECK(!px_ir_user(1, name, 0), "ir_erase: the slot reads as empty");

    printf(fails ? "%d FAILED\n" : "flash hold: every flash access of a save or an erase is inside the audio hold\n", fails);
    return fails != 0;
}
