/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth on the MPC (ports/mpc/cr_mpc.c) through its engine interface: lists set and read by choice, the
 * FM-1's value text, presets by bank and slot from a user file, state, CC read-back, sound.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "cr_inst.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static const char *get(const mpc_engine_t *e, void *m, const char *k)
{
    static char b[8192];
    b[0] = 0;
    e->get_param(m, k, b, sizeof b);
    return b;
}

int main(int argc, char **argv)
{
    const mpc_engine_t *e = mpc_engine();
    const char *dir = argc > 1 ? argv[1] : "build/host";
    char path[512], key[32];
    static uint8_t user[CR_USER_BYTES];
    static char state[8192];
    cr_patch_t pt;
    void *m, *m2;
    int16_t out[256];
    int i, pk = 0;
    FILE *f;

    /* a user file with U1-1 = "MPC SAW" (cutoff 11) */
    memset(user, 0xFF, sizeof user);
    cr_patch_init(&pt, "MPC SAW");
    pt.p[CR_CUT] = 11;
    memcpy(user + CR_FL_PRESETS, &pt, CR_PATCH_BYTES);
    /* 0.13: a wavetable file in the data folder's Wavetables */
    {
        char wp[600];
        static const uint8_t hdr[44] = {'R','I','F','F', (36 + 1200) & 0xFF, (36 + 1200) >> 8, 0, 0, 'W','A','V','E','f','m','t',' ',
            16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd','a','t','a', 1200 & 0xFF, 1200 >> 8, 0, 0};
        snprintf(wp, sizeof wp, "rm -rf '%s/Wavetables' '%s/TableIndex.txt' && mkdir -p '%s/Wavetables/Leads' '%s/Wavetables/Basses'",
                 dir, dir, dir, dir);
        CHECK(system(wp) == 0, "mkdir");
        for (int w = 0; w < 2; w++) {           /* Leads/Saw Up first on the disk, Basses/Acid first by name */
            snprintf(wp, sizeof wp, w ? "%s/Wavetables/Basses/Acid.wav" : "%s/Wavetables/Leads/Saw Up.wav", dir);
            f = fopen(wp, "wb");
            if (f) {
                fwrite(hdr, 1, sizeof hdr, f);
                for (i = 0; i < 600; i++) {
                    int16_t v = (int16_t)(w ? (i < 300 ? 20000 : -20000) : i * 100 - 30000);
                    fwrite(&v, 2, 1, f);
                }
                fclose(f);
            }
        }
    }
    snprintf(path, sizeof path, "%s/Crafty Synth.user", dir);
    f = fopen(path, "wb");
    CHECK(f && fwrite(user, 1, sizeof user, f) == sizeof user, "can't write %s", path);
    if (f)
        fclose(f);

    m = e->create(dir);
    CHECK(m != 0, "create");
    /* a list by choice: filter type 1 = LADDER */
    e->set_param(m, "ftype", "1");
    CHECK(!strcmp(get(e, m, "ftype"), "1"), "ftype reads %s", get(e, m, "ftype"));
    CHECK(!strcmp(get(e, m, "ftype_display"), "LADDER"), "ftype shows %s", get(e, m, "ftype_display"));
    /* a knob: 0..127, the FM-1's text */
    e->set_param(m, "cut", "63.6");
    CHECK(!strcmp(get(e, m, "cut"), "64"), "cut reads %s", get(e, m, "cut"));
    CHECK(strstr(get(e, m, "cut_display"), "HZ") != 0, "cut shows %s", get(e, m, "cut_display"));
    /* a matrix destination: 44 choices, the 43rd OTT */
    e->set_param(m, "dst2", "43");
    CHECK(!strcmp(get(e, m, "dst2_display"), "OTT"), "dst2 shows %s", get(e, m, "dst2_display"));
    /* presets: bank 9 (U1) slot 1 is the user file's */
    e->set_param(m, "pbank", "8");
    CHECK(!strcmp(get(e, m, "preset"), "64"), "bank 8: preset %s", get(e, m, "preset"));
    CHECK(!strcmp(get(e, m, "preset_display"), "U1-1 MPC SAW"), "preset shows %s", get(e, m, "preset_display"));
    CHECK(!strcmp(get(e, m, "cut"), "11"), "U1-1's cutoff: %s", get(e, m, "cut"));
    CHECK(!strcmp(get(e, m, "pbank_display"), "U1"), "pbank shows %s", get(e, m, "pbank_display"));
    CHECK(!strcmp(get(e, m, "preset:2"), "1-3 SYNC SWEEP"), "program 2 is %s", get(e, m, "preset:2"));
    i = 0;
    while (mpc_engine_changed(m, key, sizeof key))
        i++;
    CHECK(i >= 3 + (int)CR_NP + 2 && i <= 3 + (int)CR_NP + 8, "a preset reported %d keys", i);
    e->set_param(m, "pslot", "3");
    CHECK(!strcmp(get(e, m, "preset"), "66"), "slot 3: preset %s", get(e, m, "preset"));
    /* a CC on the track: the screen is told */
    while (mpc_engine_changed(m, key, sizeof key))
        ;
    {
        uint8_t cc[3] = {0xB0, 35, 99};          /* resonance */
        e->midi(m, cc, 3);
        CHECK(mpc_engine_changed(m, key, sizeof key) && !strcmp(key, "res"), "CC 35 reported as %s", key);
    }
    /* 0.13: the wavetable file, by number, shown by name; RANDOM */
    e->set_param(m, "table2", "1");                 /* by name: Basses/Acid, then Leads/Saw Up */
    CHECK(!strcmp(get(e, m, "table2_display"), "Acid"), "table2 1: %s", get(e, m, "table2_display"));
    e->set_param(m, "table2", "2");
    CHECK(!strcmp(get(e, m, "table2"), "2") && !strcmp(get(e, m, "table2_display"), "Saw Up"), "table2: %s %s",
          get(e, m, "table2"), get(e, m, "table2_display"));
    CHECK(!strcmp(get(e, m, "table1_display"), "BANK"), "table1 shows %s", get(e, m, "table1_display"));
    {   /* the OSC page's 3D views: wave 2 plays a file (USER view), wave 1 its factory bank */
        int saw2 = 0;
        while (mpc_engine_changed(m, key, sizeof key))
            saw2 |= !strcmp(key, "w2shown");
        CHECK(saw2 && !strcmp(get(e, m, "w2shown"), "0"), "w2shown: %s", get(e, m, "w2shown"));
        CHECK(atoi(get(e, m, "w1shown")) >= 1, "w1shown: %s", get(e, m, "w1shown"));
    }
    {
        char before[512];
        int same = 1, t;
        e->set_param(m, "rkeep", "1");
        strcpy(before, get(e, m, "state"));
        for (t = 0; t < 4 && same; t++) {
            e->set_param(m, "random", "1");
            e->set_param(m, "random", "0");
            same = !strcmp(before, get(e, m, "state"));
        }
        CHECK(!same, "RANDOM changed nothing");
        CHECK(!strcmp(get(e, m, "table2"), "2"), "RANDOM (tables kept) dropped the file");
        e->set_param(m, "rkeep", "0");
        e->set_param(m, "pslot", "4");
        e->set_param(m, "pslot", "3");
        while (mpc_engine_changed(m, key, sizeof key))
            ;
        e->set_param(m, "table2", "2");
        e->set_param(m, "res", "99");
    }
    /* 0.17: the wavetable list: 8 factory banks on OSC A, then the files in folder order */
    {
        e->set_param(m, "w1wt", "8");
        CHECK(!strcmp(get(e, m, "w1wt_display"), "Basses / Acid"), "w1wt 8: %s", get(e, m, "w1wt_display"));
        e->set_param(m, "w1wt_next", "1");
        CHECK(!strcmp(get(e, m, "w1wt"), "9") && !strcmp(get(e, m, "w1wt_display"), "Leads / Saw Up"), "next: %s %s",
              get(e, m, "w1wt"), get(e, m, "w1wt_display"));
        e->set_param(m, "w1wt_next", "1");      /* the end of the list: it stays */
        CHECK(!strcmp(get(e, m, "w1wt"), "9"), "past the end: %s", get(e, m, "w1wt"));
        e->set_param(m, "w1list", "1");
        CHECK(!strcmp(get(e, m, "w1row_8"), "Leads / Saw Up") && !strcmp(get(e, m, "w1row_8_on"), "1") &&
              !strcmp(get(e, m, "w1row_1"), "MATH"), "rows (window from MATH)");
        e->set_param(m, "w1row_3", "1");        /* a factory bank tapped: the file goes */
        CHECK(!strcmp(get(e, m, "w1wt_display"), "CURATED 1") && !strcmp(get(e, m, "table1_display"), "BANK"),
              "row 3: %s", get(e, m, "w1wt_display"));
        e->set_param(m, "w1wt", "0");
        CHECK(!strcmp(get(e, m, "w1wt_display"), "ANALOGUE"), "w1wt 0: %s", get(e, m, "w1wt_display"));
        e->set_param(m, "w2wt", "10");          /* OSC B: 9 factory banks (sine / noise too) */
        CHECK(!strcmp(get(e, m, "w2wt_display"), "Leads / Saw Up"), "w2wt 10: %s", get(e, m, "w2wt_display"));
        {   /* 0.19: its view draws the file's ramp: low columns at the left, high at the right */
            int lo = atoi(get(e, m, "w2c_2")), hi = atoi(get(e, m, "w2c_55")), mid = atoi(get(e, m, "w2c_28"));
            CHECK(lo < 6 && hi > 26 && mid > 12 && mid < 20, "w2 view columns %d %d %d", lo, mid, hi);
            CHECK(!strcmp(get(e, m, "w2shown"), "0"), "w2shown %s", get(e, m, "w2shown"));
        }
        while (mpc_engine_changed(m, key, sizeof key))
            ;
    }
    /* state into a second instance */
    e->set_param(m, "cut", "77");
    strcpy(state, get(e, m, "state"));
    m2 = e->create(dir);
    e->set_param(m2, "state", state);
    CHECK(!strcmp(get(e, m2, "cut"), "77") && !strcmp(get(e, m2, "res"), "99") && !strcmp(get(e, m2, "preset"), "66"),
          "state: cut %s res %s", get(e, m2, "cut"), get(e, m2, "res"));
    CHECK(!strcmp(get(e, m2, "w2wt_display"), "Leads / Saw Up"), "state: w2wt %s", get(e, m2, "w2wt_display"));
    /* sound */
    {
        uint8_t on[3] = {0x90, 57, 110};
        e->midi(m2, on, 3);
        for (i = 0; i < 200; i++) {
            int k;
            e->render(m2, out, 128);
            for (k = 0; k < 256; k++)
                if (abs(out[k]) > pk)
                    pk = abs(out[k]);
        }
        CHECK(pk > 1000 && pk < 32767, "peak %d", pk);
    }
    /* 0.18: polyphony: a chord on 8 voices is fuller than on MONO (one note); SAVE writes a user slot */
    {
        double r[2];
        int mode;
        for (mode = 0; mode < 2; mode++) {
            void *q = e->create(dir);
            uint8_t c[3] = {0x90, 48, 110};
            int b, k;
            e->set_param(q, "voices", mode ? "4" : "0");
            for (k = 0; k < 3; k++) {
                c[1] = (uint8_t)(48 + 4 * k);
                e->midi(q, c, 3);
            }
            r[mode] = 0.0;
            for (b = 0; b < 300; b++) {
                e->render(q, out, 128);
                if (b >= 100)
                    for (k = 0; k < 256; k++)
                        r[mode] += (double)out[k] * out[k];
            }
            e->destroy(q);
        }
        printf("mpc: a 3-note chord, MONO %.0f, 8 voices %.0f (energy)\n", r[0] / 1e9, r[1] / 1e9);
        CHECK(r[1] > r[0] * 1.6, "the chord isn't polyphonic (%.0f vs %.0f)", r[1], r[0]);
        e->set_param(m2, "sslot", "1");
        CHECK(!strcmp(get(e, m2, "sslot_display"), "U1-2 EMPTY"), "sslot: %s", get(e, m2, "sslot_display"));
        e->set_param(m2, "ssave", "1");
        CHECK(!strcmp(get(e, m2, "preset_display"), "U1-2 INIT"), "saved: %s", get(e, m2, "preset_display"));
        {
            void *q = e->create(dir);            /* a new instance reads the file */
            CHECK(!strcmp(get(e, q, "preset:65"), "U1-2 INIT"), "U1-2 from the file: %s", get(e, q, "preset:65"));
            e->destroy(q);
        }
    }
    e->destroy(m);
    e->destroy(m2);
    remove(path);
    if (fails)
        return 1;
    printf("ok   mpc (peak %d)\n", pk);
    return 0;
}
