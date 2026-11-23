/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the engine for the web editor's preview, built to WebAssembly (tools/build_wasm.sh): the firmware's
 * own engine through src/host/cr_inst.c (as the AU / VST3 and MPC plugins), so the browser plays what the FM-1 plays.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_inst.h"
#include "cr_wav.h"
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

/* ---- 0.13: the shared WAV importer (src/host/cr_wav.c) ----
 * The editor grows the memory and puts the file at cw_heap(), calls cw_wav_open(bytes) (the frames, 0: not read),
 * then cw_wav_bank(5 frames) for the 5 x 256 samples (0: silent). */
extern unsigned char __heap_base;
static cr_wav_t WAV;
static cr_wav_work_t WORK;
static int16_t BANK[5][CR_WT_N];
static uint32_t wav_read(void *ctx, uint32_t off, void *buf, uint32_t n)
{
    const cr_wav_t *w = &WAV;
    (void)ctx;
    if (off >= w->size)
        return 0;
    if (n > w->size - off)
        n = w->size - off;
    memcpy(buf, &__heap_base + off, n);
    return n;
}
EXPORT uint8_t *cw_heap(void) { return &__heap_base; }
EXPORT uint32_t cw_wav_open(uint32_t bytes) { return cr_wav_open(&WAV, wav_read, 0, bytes) ? WAV.frames : 0u; }
EXPORT uint32_t cw_wav_frame_size(void) { return WAV.frame_size; }
EXPORT const int16_t *cw_wav_bank(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t pk[5];
    pk[0] = a; pk[1] = b; pk[2] = c; pk[3] = d; pk[4] = e;
    return cr_wav_bank(&WAV, pk, BANK, &WORK) ? &BANK[0][0] : 0;
}

/* ---- 0.13: Random Patch (src/core cr_patch_random): the editor marks the saved slots, picks, loads, varies ---- */
static uint8_t SAVED[CR_NPRESET];
static cr_patch_t CUR;
static uint32_t SEED;
static int rnd_saved(void *ctx, uint32_t slot) { (void)ctx; return SAVED[slot % CR_NPRESET]; }
EXPORT uint8_t *cw_rand_saved(void) { return SAVED; }
EXPORT uint8_t *cw_rand_cur(void) { return (uint8_t *)&CUR; }
EXPORT void cw_rand_seed(uint32_t s) { SEED = s; }
EXPORT uint32_t cw_rand_slot(void) { return cr_rand_slot(&SEED, rnd_saved, 0); }
/* the preset in cw_patch() varied, the banks kept from cw_rand_cur() with flags 1 */
EXPORT void cw_rand_vary(uint32_t flags)
{
    (void)cr_patch_check(&P);
    cr_patch_random(&P, &CUR, &SEED, flags);
}

