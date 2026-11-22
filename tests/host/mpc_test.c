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
    CHECK(i == 3 + (int)CR_NP, "a preset reported %d keys", i);
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
    /* state into a second instance */
    e->set_param(m, "cut", "77");
    strcpy(state, get(e, m, "state"));
    m2 = e->create(dir);
    e->set_param(m2, "state", state);
    CHECK(!strcmp(get(e, m2, "cut"), "77") && !strcmp(get(e, m2, "res"), "99") && !strcmp(get(e, m2, "preset"), "66"),
          "state: cut %s res %s", get(e, m2, "cut"), get(e, m2, "res"));
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
    e->destroy(m);
    e->destroy(m2);
    remove(path);
    if (fails)
        return 1;
    printf("ok   mpc (peak %d)\n", pk);
    return 0;
}
