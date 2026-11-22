/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the engine for the web editor's preview, built to WebAssembly (tools/build_wasm.sh): the firmware's
 * own engine through src/host/cr_inst.c (as the AU / VST3 and MPC plugins), so the browser plays what the FM-1 plays.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_inst.h"
#include "cr_presets.h"
#define EXPORT __attribute__((visibility("default")))

void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned long n)
{
    unsigned char *a = d;
    while (n--)
        *a++ = (unsigned char)c;
    return d;
}

static cr_inst_t I;
static uint8_t USER[CR_USER_BYTES];             /* the user banks the editor gives it (no presets here) */
static cr_patch_t P;                             /* where the editor writes a whole patch (cw_patch, then cw_apply) */
static float OUT[2 * 128];

EXPORT void cw_init(void)
{
    uint32_t i;
    for (i = 0; i < CR_USER_BYTES; i++)
        USER[i] = 0xFFu;
    cr_inst_init(&I);
    cr_inst_set_user(&I, USER);
    cr_patch_init(&P, "INIT");
    cr_inst_use(&I, &P);
}
EXPORT uint8_t *cw_patch(void) { return (uint8_t *)&P; }
EXPORT const uint8_t *cw_factory(void) { return &CR_FACTORY[0][0]; }
EXPORT const int16_t *cw_wave(uint32_t b)
{
    const cr_ubank_t *u = b >= CR_NBANK ? cr_user_bank(USER, b - CR_NBANK) : 0;
    return u ? &u->w[0][0] : &cr_wt_factory(b >= CR_NBANK ? 0u : b)[0][0];
}
/* the editor writes a user bank's 5 x 256 samples here, then calls cw_ubank_changed */
EXPORT int16_t *cw_ubank(uint32_t u)
{
    cr_ubank_t *b = (cr_ubank_t *)(void *)(USER + CR_FL_BANKS + (u % CR_NUBANK) * CR_SECTOR);
    return &b->w[0][0];
}
EXPORT void cw_ubank_changed(uint32_t u)
{
    cr_ubank_t *b = (cr_ubank_t *)(void *)(USER + CR_FL_BANKS + (u % CR_NUBANK) * CR_SECTOR);
    b->magic = CR_UBANK_MAGIC;
    b->ver = 1u;
    b->name[0] = 0;
    cr_inst_user_changed(&I);
}
EXPORT void cw_apply(void) { cr_inst_use(&I, &P); }
EXPORT void cw_param(uint32_t id, uint32_t v) { cr_inst_param(&I, id, v); }
EXPORT void cw_key(uint32_t n, uint32_t v) { cr_inst_note(&I, n, v); }
EXPORT void cw_all_off(void) { cr_inst_all_off(&I); }
EXPORT void cw_ctl(uint32_t src, float v) { cr_synth_ctl(&I.syn, src, v); }
EXPORT void cw_bend(float semis) { cr_synth_bend(&I.syn, semis); }
/* n <= 128 frames at 44.1 kHz into OUT (L R) */
EXPORT float *cw_render(uint32_t n)
{
    cr_inst_render(&I, OUT, n);
    return OUT;
}
