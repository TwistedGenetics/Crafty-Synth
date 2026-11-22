/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the engine for the web editor's preview, built to WebAssembly (tools/build_wasm.sh): the firmware's
 * own cr_synth.c, cr_wt.c, cr_arp.c and cr_patch.c, so the browser plays what the FM-1 plays.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_synth.h"
#include "cr_arp.h"
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

static cr_synth_t S;
static cr_arp_t A;
static int16_t DLY[CR_SYN_MEM];
static cr_wtbank_t T[2];
static cr_patch_t P;
static int16_t UB[CR_NUBANK][5][CR_WT_N];
static float OUT[2 * 128];
static uint32_t tb[2] = {0xFF, 0xFF};

static void a_on(uint32_t n, uint32_t v) { cr_synth_on(&S, n, v); }
static void a_off(uint32_t n) { cr_synth_off(&S, n); }
static const cr_arp_out_t AO = {a_on, a_off};

static const int16_t (*base(uint32_t b))[CR_WT_N] { return b >= CR_NBANK ? (const int16_t (*)[CR_WT_N])UB[(b - CR_NBANK) % CR_NUBANK] : cr_wt_factory(b); }

EXPORT void cw_tables(void)
{
    uint32_t b1 = cr_bank_of(P.p[CR_W1BANK], 0), b2 = cr_bank_of(P.p[CR_W2BANK], 1);
    if (tb[0] != b1) {
        cr_wt_build(&T[0], base(b1));
        tb[0] = b1;
    }
    if (tb[1] != b2) {
        cr_wt_build(&T[1], base(b2));
        tb[1] = b2;
    }
    cr_synth_tables(&S, &T[0], &T[1], b2 == CR_BANK_NOISE);
}
EXPORT void cw_init(void)
{
    uint32_t u, w, i;
    cr_synth_init(&S, DLY);
    cr_arp_init(&A, &AO);
    for (u = 0; u < CR_NUBANK; u++)
        for (w = 0; w < 5u; w++)
            for (i = 0; i < CR_WT_N; i++)
                UB[u][w][i] = cr_wt_factory(0)[w][i];
    cr_patch_init(&P, "INIT");
    cr_synth_patch(&S, &P);
    cw_tables();
}
EXPORT uint8_t *cw_patch(void) { return (uint8_t *)&P; }
EXPORT const uint8_t *cw_factory(void) { return &CR_FACTORY[0][0]; }
EXPORT const int16_t *cw_wave(uint32_t b) { return &base(b)[0][0]; }
EXPORT int16_t *cw_ubank(uint32_t u) { return &UB[u % CR_NUBANK][0][0]; }
EXPORT void cw_ubank_changed(uint32_t u)
{
    uint32_t k;
    for (k = 0; k < 2u; k++)
        if (tb[k] == CR_NBANK + u)
            tb[k] = 0xFF;
    cw_tables();
}
EXPORT void cw_apply(void)
{
    (void)cr_patch_check(&P);
    cr_synth_patch(&S, &P);
    if (P.p[CR_ARP] < 64u)
        cr_arp_release_all(&A);
    cw_tables();
}
EXPORT void cw_param(uint32_t id, uint32_t v)
{
    if (id >= CR_NP)
        return;
    P.p[id] = (uint8_t)(v & 0x7Fu);
    cr_synth_param(&S, id, v);
    if (id == CR_W1BANK || id == CR_W2BANK)
        cw_tables();
    if (id == CR_ARP && v < 64u)
        cr_arp_release_all(&A);
}
EXPORT void cw_key(uint32_t n, uint32_t v)
{
    if (P.p[CR_ARP] >= 64u)
        cr_arp_key(&A, n, v);
    else if (v)
        cr_synth_on(&S, n, v);
    else
        cr_synth_off(&S, n);
}
EXPORT void cw_all_off(void)
{
    cr_arp_release_all(&A);
    cr_synth_all_off(&S);
}
EXPORT void cw_ctl(uint32_t src, float v) { cr_synth_ctl(&S, src, v); }
EXPORT void cw_bend(float semis) { cr_synth_bend(&S, semis); }
/* n <= 128 frames at 44.1 kHz into OUT (L R) */
EXPORT float *cw_render(uint32_t n)
{
    if (n > 128u)
        n = 128u;
    cr_synth_tempo(&S, cr_arp_bpm(&P));
    cr_arp_run(&A, &P, n);
    cr_synth_render(&S, OUT, n);
    return OUT;
}
