/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the app's entry points. src/hardware/cr_main_fm1.c calls them on the FM-1; tests/host/cr_host.c on
 * the host. Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "cr_patch.h"
#include "cr_arp.h"
#include "cr_synth.h"

void cr_app_init(void);                                  /* main loop, before the audio sounds */
void cr_app_render(int32_t *lr, uint32_t n);             /* audio interrupt: n stereo frames, Q15 in int32 */
void cr_app_midi(uint8_t st, uint8_t d1, uint8_t d2);    /* audio interrupt: a channel message in */
void cr_app_realtime(uint8_t b);                         /* audio interrupt: F8 clock, FA start, FC stop */
void cr_app_input(void);                                 /* main loop: the panel */
void cr_app_draw(int force);                             /* main loop, ~30 a second */
void cr_app_panic(void);                                 /* everything off (an install is about to start) */
/* the user area in flash (the FM-1's spare sample-slot space, 0xA0000..0xDBFFF), given by the platform:
 * cr_main_fm1.c on the FM-1, a RAM image on the host. Offsets are from the start of the area. */
#define CR_FLASH_SIZE 0x3C000u                           /* 240 KB */
const uint8_t *cr_flash_ptr(uint32_t off);               /* read in place */
int cr_flash_erase(uint32_t off);                        /* a 4 KB sector (audio stops for it); 0 = ok */
int cr_flash_write(uint32_t off, const void *src, uint32_t n);   /* into erased flash; 0 = ok */
/* the editor's SysEx commands (src/app/cr_editor.c): 0 not ours, 1 a reply was put, 2 done without a reply */
int cr_ed_cmd(uint32_t cmd, const uint8_t *a, uint32_t na, void (*put)(uint32_t));
int cr_ed_long(uint32_t cmd);                            /* commands whose frames are long (handled in place) */
