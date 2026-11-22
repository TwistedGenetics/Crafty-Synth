/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the engine measured on the host: every modifier on every bank (no NaN, no silence, no overload), the
 * envelopes, the matrix, glide, spread, the time it takes. Writes WAVs of a demo phrase per modifier to OUTDIR.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cr_synth.h"
#include "cr_presets.h"

static cr_synth_t S;
static int16_t DLY[CR_DLY_LEN];
static cr_wtbank_t T[CR_BANKS];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static float run(uint32_t frames, float *rms)
{
    static float lr[256];
    float pk = 0.f, sq = 0.f;
    uint32_t i, k;
    for (i = 0; i < frames; i += 128) {
        cr_synth_render(&S, lr, 128);
        for (k = 0; k < 256u; k++) {
            float a = lr[k] < 0.f ? -lr[k] : lr[k];
            if (!(lr[k] == lr[k]))
                return -1.f;                   /* NaN */
            pk = a > pk ? a : pk;
            sq += lr[k] * lr[k];
        }
    }
    if (rms)
        *rms = sq / (float)(2u * frames);
    return pk;
}

static void use(const cr_patch_t *p)
{
    uint32_t b1 = cr_bank_of(p->p[CR_W1BANK], 0), b2 = cr_bank_of(p->p[CR_W2BANK], 1);
    cr_synth_patch(&S, p);
    cr_synth_tables(&S, &T[b1], &T[b2], b2 == CR_BANK_NOISE);
}

static void wav(const char *path, const cr_patch_t *p)
{
    static const uint8_t notes[8] = {41, 41, 53, 44, 41, 48, 46, 36};
    FILE *f = fopen(path, "wb");
    uint32_t frames = 8u * 11025u, i, k, n;
    static float lr[256];
    uint32_t v;
    if (!f)
        return;
    use(p);
    fwrite("RIFF", 1, 4, f); v = 36u + frames * 4u; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f); } v = 44100; fwrite(&v, 4, 1, f); v = 44100u * 4u; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {4, 16}; fwrite(h, 2, 2, f); } fwrite("data", 1, 4, f); v = frames * 4u; fwrite(&v, 4, 1, f);
    for (n = 0; n < 8u; n++) {
        cr_synth_on(&S, notes[n], 100);
        for (i = 0; i < 11025u; i += 128) {
            if (i == 7040u)
                cr_synth_off(&S, notes[n]);
            cr_synth_render(&S, lr, 128);
            for (k = 0; k < 256u; k++) {
                float x = lr[k] * 32767.f;
                int16_t s16 = (int16_t)(x > 32767.f ? 32767.f : x < -32767.f ? -32767.f : x);
                fwrite(&s16, 2, 1, f);
            }
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    cr_patch_t p;
    uint32_t b, m, i;
    float pk, rms;
    char path[256];
    for (b = 0; b < CR_NBANK; b++)
        cr_wt_build(&T[b], cr_wt_factory(b));
    T[9] = T[0];
    T[10] = T[1];
    cr_synth_init(&S, DLY);
    /* every modifier, every bank on Wave 1 (Wave 2 the same bank, a fifth up), with some amount */
    for (m = 0; m < CR_NOM; m++)
        for (b = 0; b < CR_NBANK; b++) {
            cr_patch_init(&p, "T");
            p.p[CR_OMMODE] = (uint8_t)(m * 8u + 4u);
            p.p[CR_OMAMT] = 80;
            p.p[CR_W1BANK] = (uint8_t)cr_bank_val(b == 8u ? 7u : b, 0);
            p.p[CR_W2BANK] = (uint8_t)cr_bank_val(b, 1);
            p.p[CR_TUNE] = 64 + 9;
            p.p[CR_W1POS] = (uint8_t)(b * 13u + 10u);
            use(&p);
            cr_synth_on(&S, 45, 100);
            pk = run(22050, &rms);
            cr_synth_off(&S, 45);
            (void)run(22050, 0);
            CHECK(pk >= 0.f, "NaN: modifier %s bank %u", CR_OM_NAME[m], b);
            CHECK(pk > 0.02f, "silent: modifier %s bank %u (peak %f)", CR_OM_NAME[m], b, (double)pk);
            CHECK(pk <= 1.f, "over: modifier %s bank %u", CR_OM_NAME[m], b);
        }
    /* the release ends: silence after */
    cr_patch_init(&p, "T");
    use(&p);
    cr_synth_on(&S, 60, 100);
    (void)run(4410, 0);
    cr_synth_off(&S, 60);
    (void)run(44100, 0);
    pk = run(4410, 0);
    CHECK(pk < 0.001f, "release does not end (%f)", (double)pk);
    CHECK(!cr_synth_sounding(&S), "still sounding after its release");
    /* AMP EG amount at 0: it drones */
    p.p[CR_AEGAMT] = 64;
    use(&p);
    pk = run(4410, 0);
    CHECK(pk > 0.05f, "no drone at AMP EG 0 (%f)", (double)pk);
    /* the filter closes */
    cr_patch_init(&p, "T");
    p.p[CR_CUT] = 10;
    use(&p);
    cr_synth_on(&S, 60, 100);
    (void)run(4410, 0);
    pk = run(4410, &rms);
    cr_synth_off(&S, 60);
    {
        float r2;
        p.p[CR_CUT] = 127;
        use(&p);
        cr_synth_on(&S, 60, 100);
        (void)run(4410, 0);
        (void)run(4410, &r2);
        cr_synth_off(&S, 60);
        CHECK(rms < r2 * 0.1f, "cutoff 10 not much darker than open (%f vs %f)", (double)rms, (double)r2);
    }
    /* glide: from 36 to 72 over time */
    cr_patch_init(&p, "T");
    p.p[CR_GLIDE] = 110;
    use(&p);
    cr_synth_on(&S, 36, 100);
    (void)run(4410, 0);
    cr_synth_on(&S, 72, 100);
    (void)run(2205, 0);
    CHECK(S.pitch > 37.f && S.pitch < 71.f, "glide: pitch %f after 50 ms", (double)S.pitch);
    cr_synth_all_off(&S);
    /* the matrix: LFO 1 on cutoff moves it */
    cr_patch_init(&p, "T");
    p.p[CR_DST0] = CR_D_CUT;
    p.p[CR_DEP0] = 127;
    p.p[CR_CUT] = 64;
    p.p[CR_L1RATE] = 90;
    use(&p);
    cr_synth_on(&S, 48, 100);
    {
        float lo = 1.f, hi = 0.f;
        for (i = 0; i < 200u; i++) {
            (void)run(128, 0);
            lo = S.eff[CR_D_CUT] < lo ? S.eff[CR_D_CUT] : lo;
            hi = S.eff[CR_D_CUT] > hi ? S.eff[CR_D_CUT] : hi;
        }
        CHECK(lo < 0.1f && hi > 0.9f, "LFO 1 at full depth on cutoff at the middle: %f..%f", (double)lo, (double)hi);
    }
    cr_synth_all_off(&S);
    /* the heaviest: spread chord, FM, noise on wave 2, delay, distortion: the time it takes */
    cr_patch_init(&p, "T");
    p.p[CR_SPREAD] = 100;
    p.p[CR_OMMODE] = CR_OM_VOCAL * 8u;
    p.p[CR_OMAMT] = 90;
    p.p[CR_DIST] = 60;
    p.p[CR_DMIX] = 50;
    p.p[CR_W2BANK] = (uint8_t)cr_bank_val(CR_BANK_NOISE, 1);
    p.p[CR_DST0] = CR_D_W1;
    p.p[CR_DEP0] = 100;
    use(&p);
    cr_synth_on(&S, 48, 100);
    {
        clock_t t0 = clock();
        (void)run(441000, 0);
        double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / 441000.0;
        printf("     the heaviest patch: %.0f ns a frame on this PC (the FM-1's budget is ~22,700 ns)\n", ns);
    }
    cr_synth_all_off(&S);
    /* every factory preset: a note sounds, not too quiet, not overloaded */
    for (m = 0; m < CR_NPRESET; m++) {
        for (i = 0; i < sizeof p; i++)
            ((uint8_t *)&p)[i] = CR_FACTORY[m][i];
        CHECK(cr_patch_check(&p) == 0, "preset %u is not a patch", m + 1u);
        cr_synth_init(&S, DLY);
        use(&p);
        cr_synth_on(&S, 48, 100);
        pk = run(22050, &rms);
        cr_synth_off(&S, 48);
        (void)run(4410, 0);
        printf("     preset %2u %-11s peak %.2f rms %.3f\n", m + 1u, p.name, (double)pk, (double)rms);
        CHECK(pk > 0.05f && pk <= 1.f, "preset %u %s: peak %f", m + 1u, p.name, (double)pk);
    }
    if (argc > 1)
        for (m = 0; m < CR_NOM; m++) {
            cr_patch_init(&p, "T");
            p.p[CR_OMMODE] = (uint8_t)(m * 8u + 4u);
            p.p[CR_OMAMT] = 70;
            p.p[CR_TUNE] = 64 + 16;
            p.p[CR_DST0] = CR_D_OMAMT;
            p.p[CR_DEP0] = 100;
            p.p[CR_L1RATE] = 50;
            p.p[CR_SPREAD] = 30;
            p.p[CR_CUT] = 100;
            p.p[CR_DMIX] = 25;
            sprintf(path, "%s/mod_%02u.wav", argv[1], m + 1u);
            wav(path, &p);
        }
    printf("%s synth: %d failure(s)\n", fails ? "FAIL" : "ok  ", fails);
    return fails != 0;
}
