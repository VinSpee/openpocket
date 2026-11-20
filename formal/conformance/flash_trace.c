/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the order in which the firmware's own flash code holds the audio and takes the flash, recorded for
 * formal/FlashHold.tla (formal/check.mjs runs `everystate conform` on what this writes).
 *
 * It runs firmware/px_store.h, firmware/px_models.h and firmware/px_irs.h on a PC, as tests/test_store.c does, on a
 * 1 MiB flash array. What st_audio_hold and the flash routines call on the pedal writes one line here, named after
 * the model's step: fm1_irq_mask "Mask", a_collect "CollectBegin", fm1_irq_unmask "Unmask", and each fl_erase4k,
 * fl_write and ota_fread the SFC off and on again (the model's FlashOff and FlashOn).
 *   cc -O1 -Iengine formal/conformance/flash_trace.c engine/px_dsp.c firmware/px_math.c -lm -o flash_trace
 *   flash_trace DIR      writes DIR/st_save.jsonl, nm_commit.jsonl, nm_erase.jsonl, ir_commit.jsonl, ir_erase.jsonl */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "px_nam.h"
#include "../../firmware/px_midi.h"

static uint8_t nor[0x100000];
static FILE *out;                                  /* 0: not recording (the start-up reads) */
static void step(const char *line) { if (out) fprintf(out, "%s\n", line); }
static void flash_off(void) { step("{\"action\": \"FlashOff\", \"state\": {\"sfc\": \"off\"}}"); }
static void flash_on(void) { step("{\"state\": {\"sfc\": \"on\"}}"); }

/* ---- the firmware pieces the three files use ---- */
static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t c)
{
    while (n--) { c ^= (uint32_t)*p++ << 8; for (int i = 0; i < 8; i++) c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1; c &= 0xFFFFu; }
    return c;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { flash_off(); memcpy(p, nor + off, n); flash_on(); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *t) { (void)t; flash_off(); memset(nor + off, 0xFF, 0x1000); flash_on(); return 0; }
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    flash_off();
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= s[i];
    flash_on();
    return 0;
}
static volatile uint8_t a_mute;
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(int ms) { (void)ms; }
#define FM1_IRQ_ALNK0 11
static void fm1_irq_mask(int n) { (void)n; step("{\"action\": \"Mask\", \"state\": {\"masked\": true}}"); }
static void fm1_irq_unmask(int n) { (void)n; step("{\"action\": \"Unmask\", \"state\": {\"masked\": false}}"); }
static void a_collect(void) { step("{\"action\": \"CollectBegin\"}"); }
static uint8_t px_flash_ok = 1;
static uint32_t cur_preset;
static px_preset_t PRESET;
static void st_get(uint32_t k, px_preset_t *p);
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
#define ST_IRQ_OFF() ((void)0)
#include "../../firmware/px_store.h"
#include "../../firmware/px_models.h"
#include "../../firmware/px_irs.h"

static const char *dir;
static void record(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.jsonl", dir, name);
    out = fopen(path, "w");
    if (!out) { perror(path); exit(2); }
    step("{\"state\": {\"masked\": false, \"sfc\": \"on\"}}");
}
static int done(int rc, const char *name)
{
    fclose(out);
    out = 0;
    if (rc) fprintf(stderr, "flash_trace: %s returned %d\n", name, rc);
    return rc != 0;
}

int main(int argc, char **argv)
{
    int bad = 0;
    if (argc != 2) { fprintf(stderr, "usage: flash_trace DIR\n"); return 2; }
    dir = argv[1];
    memset(nor, 0xFF, sizeof nor);
    px_store_boot();
    nm_scan();
    ir_scan();

    record("st_save");
    bad |= done(st_save(3, &PRESET), "st_save");

    {   /* a model the commit takes: its header, finite weights, their CRC */
        nm_hdr_t *h = (nm_hdr_t *)nm_stage;
        memset(nm_stage, 0, sizeof nm_stage);
        h->magic = NM_MAGIC; h->ver = 1; h->nw = PXN_NW;
        memcpy(h->name, "TRACE", 6);
        h->crc = (uint16_t)ota_crc16(nm_stage + NM_HDR, PXN_NW * 4u, 0);
    }
    record("nm_commit");
    bad |= done(nm_commit(2), "nm_commit");
    record("nm_erase");
    bad |= done(nm_erase(2), "nm_erase");

    {   /* an IR the commit takes */
        ir_hdr_t *h = (ir_hdr_t *)ir_stage;
        memset(ir_stage, 0, IR_BYTES);
        h->magic = IR_MAGIC; h->ver = 1; h->n = PX_IR_LEN; h->scale = 1.0f;
        memcpy(h->name, "TRACE", 6);
        h->crc = (uint16_t)ota_crc16(ir_stage + IR_HDR, PX_IR_LEN * 2u, 0);
    }
    record("ir_commit");
    bad |= done(ir_commit(1), "ir_commit");
    record("ir_erase");
    bad |= done(ir_erase(1), "ir_erase");
    return bad;
}
