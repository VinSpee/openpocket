/* SPDX-License-Identifier: GPL-3.0-only */
/* PALETTE's own memory regions (firmware/app.ld includes this file; docs/BASE-FIRMWARE.md): the 432 KiB of SRAM from
 * 0x01C08000 up to the stacks. The engine's state is 124 KiB of the POOL (firmware/apps/palette/engine.h); the rest of
 * the POOL is kept for a looper's memory. The RAM code window holds the twenty effects' loops (22 KiB with the base's
 * audio path), with room to voice them. */
MEMORY
{
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 64K       /* .data, .bss */
    POOL   (rw)  : ORIGIN = 0x01C18000, LENGTH = 0x4C000   /* 304 KiB for the engine (section .pool) */
    RAMTEXT (rwx): ORIGIN = 0x01C64000, LENGTH = 0x10000   /* 64 KiB of RAM code, up to the stacks */
}
