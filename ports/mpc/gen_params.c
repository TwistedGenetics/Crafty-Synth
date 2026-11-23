/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth on the MPC: writes params.json (the toolkit's parameter list, tools/params.py) from the engine's
 * own tables, so every engine parameter reaches the MPC: the preset first, then the patch values in the engine's
 * order (append only, as the engine's), lists with their choices, sections that become the skin's pages.
 * Built and run by ports/mpc/make_port.py. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include "cr_patch.h"
#include "cr_keys.h"
#include "cr_mpc_lists.h"
#include "cr_tables.h"

static void js(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

/* the skin's pages: two sections of at most 8 controls make one page of two Q-Link banks */
static const char *const SECTIONS[][9] = {
    {"OSC A", "W1POS", "W1BANK", "OMAMT", "OMMODE", "MIX", "SPREAD", "GLIDE", "OCT"},
    {"OSC B", "W2POS", "W2BANK", "BMODE", "BAMT", "TUNE", "FINE", 0},
    {"SUB OSC", "SUBLVL", "SUBOCT", "SUBWAVE", 0},
    {"FILTER", "CUT", "RES", "MORPH", "FTYPE", "FEGAMT", "DIST", 0},
    {"FILTER EG", "FA", "FD", "FS", "FR", 0},
    {"AMP EG", "AA", "AD", "AS", "AR", "AEGAMT", "VOL", 0},
    {"MOD EG", "MA", "MD", "MS", "MR", 0},
    {"LFO 1", "L1RATE", "L1SHAPE", "L1MODE", "L1SYNC", 0},
    {"LFO 2", "L2RATE", "L2SHAPE", "L2MODE", "L2SYNC", 0},
    {"MATRIX LFO 1 - NOTE", "DST0", "DEP0", "DST1", "DEP1", "DST2", "DEP2", "DST3", "DEP3"},
    {"MATRIX VELO - AFTTCH", "DST4", "DEP4", "DST5", "DEP5", "DST6", "DEP6", "DST7", "DEP7"},
    {"REVERB", "VMODE", "VSIZE", "VDECAY", "VMIX", "VWARP", "VDENS", "VMOD", 0},
    {"DELAY", "DMIX", "DTIME", "DFB", "DSYNC", 0},
    {"MOD FX", "CHMIX", "CHRATE", "PHMIX", "PHRATE", 0},
    {"CRUSH", "ERA", "CRUSH", "OTT", 0},
    {"PRESET", "@pbank", "@pslot", "@preset", "@random", "@rkeep", "@rdepth", 0},
    {"ARP-SEQ", "ARP", "TEMPO", "ARPRATE", 0},
};

int main(void)
{
    uint32_t i, k, s, placed[CR_NP] = {0};
    mpc_list_t l;
    char key[16];
    printf("{\n \"name\": \"Crafty Synth\",\n \"params\": [\n");
    /* the preset three ways: BANK (1-8 factory, U1-U16 user) and SLOT step one a click; PRESET (all 192, MPC's
     * PRESET menu) moves one a Q-Link event or wheel click with nudge_pct */
    printf("  {\"key\": \"pbank\", \"name\": \"BANK\", \"min\": 0, \"max\": %u, \"default\": 0, \"display\": \"int\", "
           "\"dynamic_display\": true, \"refresh_display\": true},\n", CR_NPRESET / 8u - 1u);
    printf("  {\"key\": \"pslot\", \"name\": \"SLOT\", \"min\": 1, \"max\": 8, \"default\": 1, \"display\": \"int\", "
           "\"refresh_display\": true},\n");
    printf("  {\"key\": \"preset\", \"name\": \"PRESET\", \"min\": 0, \"max\": %u, \"default\": 0, \"display\": \"int\", "
           "\"dynamic_display\": true, \"refresh_display\": true, \"nudge_pct\": 10}",
           CR_NPRESET - 1u);
    for (i = 0; i < CR_NP; i++) {
        char name[32];
        const char *pg = cr_param_page(i);
        mpc_key(CR_KEY[i], key);
        if (i >= CR_DEP0 && i < CR_DST0 + CR_NSRC) {      /* the matrix: its source, no spaces ("MODWHL DEST") */
            char src[16];
            uint32_t a = 0, b;
            const char *sn = CR_SRC_NAME[(i - (i >= CR_DST0 ? CR_DST0 : CR_DEP0)) % CR_NSRC];
            for (b = 0; sn[b] && a < sizeof src - 1u; b++)
                if (sn[b] != ' ')
                    src[a++] = sn[b];
            src[a] = 0;
            snprintf(name, sizeof name, "%s %s", src, CR_P[i].name);
        } else if (pg[0] && strncmp(CR_P[i].name, pg, strlen(pg)))
            snprintf(name, sizeof name, "%s %s", pg, CR_P[i].name);
        else
            snprintf(name, sizeof name, "%s", CR_P[i].name);
        printf(",\n  {\"key\": ");
        js(key);
        printf(", \"name\": ");
        js(name);
        mpc_list(i, &l);
        if (l.n > MPC_MAXOPT) {
            fprintf(stderr, "gen_params: %s has more than %u choices: raise MPC_MAXOPT\n", CR_KEY[i], MPC_MAXOPT);
            return 1;
        }
        if (l.n) {
            printf(", \"options\": [");
            for (k = 0; k < l.n; k++) {
                if (k)
                    printf(", ");
                js(l.name[k]);
            }
            printf("], \"default\": %u", l.of[CR_P[i].def]);
            if (CR_P[i].kind == CR_K_BANK)
                printf(", \"dynamic_display\": true");      /* a user bank shows its name */
        } else
            printf(", \"min\": 0, \"max\": 127, \"default\": %u, \"dynamic_display\": true", CR_P[i].def);
        printf("}");
    }
    /* 0.13: the wavetable files (0.15: their place in the folder by name, 0 = the bank, shown by name), Random Patch */
    printf(",\n  {\"key\": \"table1\", \"name\": \"TABLE 1\", \"min\": 0, \"max\": %u, \"default\": 0, "
           "\"display\": \"int\", \"dynamic_display\": true, \"nudge_pct\": 1}", CR_TABLES_MAX);
    printf(",\n  {\"key\": \"table2\", \"name\": \"TABLE 2\", \"min\": 0, \"max\": %u, \"default\": 0, "
           "\"display\": \"int\", \"dynamic_display\": true, \"nudge_pct\": 1}", CR_TABLES_MAX);
    printf(",\n  {\"key\": \"random\", \"name\": \"RANDOM\", \"min\": 0, \"max\": 1, \"default\": 0, \"type\": \"trigger\", "
           "\"momentary\": true}");
    printf(",\n  {\"key\": \"rkeep\", \"name\": \"RND TABLES\", \"options\": [\"NEW\", \"KEEP\"], \"default\": 0}");
    /* 0.16: RANDOM's depth (0: the Tracker's light touch .. 127: anything) */
    printf(",\n  {\"key\": \"rdepth\", \"name\": \"RND DEPTH\", \"min\": 0, \"max\": 127, \"default\": 32, "
           "\"dynamic_display\": true}");
    /* the 3D wavetable views (make_port.py's OSC page): which bank each oscillator shows, set by the engine
     * (0: a user bank or a wavetable file, 1..: factory bank n - 1); no control, the views follow it */
    for (k = 1; k <= 2; k++) {
        printf(",\n  {\"key\": \"w%ushown\", \"name\": \"VIEW %u\", \"options\": [\"USER\"", k, k);
        for (i = 0; i < (k == 1 ? CR_BANK_NOISE : CR_NBANK); i++) {
            printf(", ");
            js(CR_BANK_NAME[i]);
        }
        printf("], \"default\": 1}");
    }
    printf("\n ],\n \"sections\": [\n");
    for (s = 0; s < sizeof SECTIONS / sizeof SECTIONS[0]; s++) {
        printf("%s  {\"label\": ", s ? ",\n" : "");
        js(SECTIONS[s][0]);
        printf(", \"keys\": [");
        for (k = 1; k < 9 && SECTIONS[s][k]; k++) {
            const char *n = SECTIONS[s][k];
            if (k > 1)
                printf(", ");
            if (n[0] == '@') {
                js(n + 1);
                continue;
            }
            for (i = 0; i < CR_NP && strcmp(CR_KEY[i], n); i++)
                ;
            if (i == CR_NP) {
                fprintf(stderr, "gen_params: no parameter %s\n", n);
                return 1;
            }
            placed[i] = 1;
            mpc_key(CR_KEY[i], key);
            js(key);
        }
        printf("]}");
    }
    printf("\n ]\n}\n");
    for (i = 0; i < CR_NP; i++)
        if (!placed[i]) {
            fprintf(stderr, "gen_params: parameter %s is on no page: add it to SECTIONS\n", CR_KEY[i]);
            return 1;
        }
    return 0;
}
