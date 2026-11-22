/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth on the MPC: which parameters are lists (a bank, a modifier, a filter type ...) and their choices,
 * worked out from the engine's own value text, so the MPC's lists follow the engine (tools: gen_params.c; the
 * plugin: cr_mpc.c). A list's choices are the runs of 0..127 that show the same text; each is set by the middle of
 * its run. Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdio.h>
#include <string.h>
#include "cr_text.h"

#define MPC_MAXOPT 64

typedef struct {
    uint8_t n;                     /* choices (0: not a list, a 0..127 knob) */
    uint8_t rep[MPC_MAXOPT];       /* the value that sets each */
    uint8_t of[128];               /* the choice each value shows */
    char name[MPC_MAXOPT][24];
} mpc_list_t;

static const char *mpc_no_names(void *ctx, uint32_t u)
{
    (void)ctx;
    (void)u;
    return "";                     /* a user bank by its number ("USER 3"), not its name */
}

static int mpc_is_list(uint32_t id)
{
    switch (CR_P[id].kind) {
    case CR_K_BANK: case CR_K_OMMODE: case CR_K_LSHAPE: case CR_K_LMODE: case CR_K_ONOFF: case CR_K_DEST:
    case CR_K_ARPRATE: case CR_K_BMODE: case CR_K_FTYPE: case CR_K_VMODE: case CR_K_ERA:
        return 1;
    default:
        return 0;
    }
}

static void mpc_list(uint32_t id, mpc_list_t *l)
{
    cr_patch_t p;
    uint32_t v, start = 0;
    char prev[48] = "", t[48];
    memset(l, 0, sizeof *l);
    if (!mpc_is_list(id))
        return;
    cr_patch_init(&p, "");
    for (v = 0; v <= 128u; v++) {
        if (v < 128u) {
            p.p[id] = (uint8_t)v;
            cr_param_text(&p, id, t, 1, mpc_no_names, 0);
        }
        if (v == 128u || (v && strcmp(t, prev) != 0)) {   /* a run ends */
            uint32_t k, c;
            for (c = 0; c < l->n && strcmp(l->name[c], prev); c++)
                ;                          /* a name seen before (a value that wraps round): that choice */
            if (c == l->n && l->n < MPC_MAXOPT) {
                l->rep[l->n] = (uint8_t)((start + v - 1u) / 2u);
                snprintf(l->name[l->n], sizeof l->name[0], "%s", prev);
                l->n++;
            }
            for (k = start; k < v; k++)
                l->of[k] = (uint8_t)(c < l->n ? c : l->n - 1u);
            start = v;
        }
        if (v < 128u)
            strcpy(prev, t);
    }
}

/* the MPC's key for parameter id: its name in cr_patch.h, lower case ("w1pos", "cut") */
static void mpc_key(const char *key, char *out)
{
    uint32_t i;
    for (i = 0; key[i] && i < 15u; i++)
        out[i] = (char)(key[i] >= 'A' && key[i] <= 'Z' ? key[i] - 'A' + 'a' : key[i]);
    out[i] = 0;
}
