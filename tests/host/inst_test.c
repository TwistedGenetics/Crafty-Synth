/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the plugins' shared instance (src/host/cr_inst.c) and protocol (cr_proto.c) checked: two instances
 * side by side, MIDI in, CC read-back, presets, the user area through the editor's commands, the arp locked to a
 * host's beat. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cr_proto.h"
#include "cr_presets.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t rep[4096];
static uint32_t nrep;
static void put(void *ctx, uint32_t b) { (void)ctx; if (nrep < sizeof rep) rep[nrep++] = (uint8_t)b; }
static void pack(const uint8_t *b, uint32_t n, uint8_t *out, uint32_t *no)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 7u) {
        uint32_t m = 0, k = n - i < 7u ? n - i : 7u;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(b[i + j] >> 7) << j;
        out[(*no)++] = (uint8_t)m;
        for (j = 0; j < k; j++)
            out[(*no)++] = b[i + j] & 0x7Fu;
    }
}
static float level(cr_inst_t *in, uint32_t blocks)
{
    float lr[256], pk = 0.f;
    uint32_t b, i;
    for (b = 0; b < blocks; b++) {
        cr_inst_render(in, lr, 128);
        for (i = 0; i < 256u; i++)
            if (fabsf(lr[i]) > pk)
                pk = fabsf(lr[i]);
    }
    return pk;
}
static void midi3(cr_inst_t *in, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t m[3] = {a, b, c};
    cr_inst_midi(in, m, 3);
}

int main(void)
{
    cr_inst_t *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    cr_store_t *st = malloc(sizeof *st);
    uint8_t args[512];
    uint32_t na, id;
    cr_patch_t t;
    float pk;
    int notes = 0;
    printf("inst: %u bytes an instance, %u the store\n", (unsigned)sizeof *a, (unsigned)sizeof *st);

    cr_store_init(st, "Crafty Synth TEST");
    cr_inst_init(a);
    cr_inst_init(b);
    cr_inst_set_user(a, st->user);
    cr_inst_set_user(b, st->user);

    /* sound, and silence after */
    CHECK(level(a, 20) < 1e-4f, "a silent instance makes sound");
    midi3(a, 0x90, 60, 100);
    pk = level(a, 60);
    CHECK(pk > 0.05f && pk < 4.f, "note on: peak %.3f", (double)pk);
    CHECK(level(b, 20) < 1e-4f, "instance b sounds from a's note");
    midi3(a, 0x80, 60, 0);
    (void)level(a, 400);
    CHECK(level(a, 10) < 1e-3f, "note off: still sounding");

    /* a CC is a parameter, read back by the host */
    midi3(a, 0xB0, 34, 20);                      /* cutoff */
    CHECK(a->p.p[CR_CUT] == 20, "CC 34 didn't set CUT");
    id = cr_inst_take_changed(a);
    CHECK(id == CR_CUT + 1u, "changed: %u", id);
    CHECK(cr_inst_take_changed(a) == 0, "changed twice");

    /* sustain */
    midi3(a, 0xB0, 64, 127);
    midi3(a, 0x90, 64, 100);
    midi3(a, 0x80, 64, 0);
    CHECK(level(a, 200) > 0.02f, "sustain didn't hold");
    midi3(a, 0xB0, 64, 0);
    (void)level(a, 600);
    CHECK(level(a, 10) < 1e-3f, "sustain released: still sounding");

    /* program change: a factory preset */
    midi3(a, 0xC0, 9, 0);
    CHECK(a->cur == 9 && memcmp(a->p.name, CR_FACTORY[9], 12) == 0, "program change 9: %u %.12s", a->cur, a->p.name);
    CHECK(cr_inst_take_patch_changed(a) == 1 && cr_inst_take_patch_changed(a) == 0, "patch_changed");

    /* the protocol: INFO */
    nrep = 0;
    CHECK(cr_proto_cmd(st, a, CR_ED_INFO, 0, 0, put, 0) == 1, "INFO");
    CHECK(strcmp((char *)rep, "Crafty Synth TEST") == 0 && rep[18] == 2 && rep[19] == 64 && (rep[20] | rep[21] << 7) == 128,
          "INFO reply");
    /* save the working patch to U1-1 (slot 64), read it back, select it on b */
    a->p.p[CR_RES] = 99;
    na = 0;
    args[na++] = 64; args[na++] = 0;
    pack((const uint8_t *)&a->p, CR_PATCH_BYTES, args, &na);
    nrep = 0;
    CHECK(cr_proto_cmd(st, a, CR_ED_PPUT, args, na, put, 0) == 1 && rep[2] == 0, "PPUT U1-1");
    CHECK(st->user_dirty && cr_user_saved(st->user, 64), "U1-1 not saved");
    args[0] = 3; args[1] = 0;                    /* a factory slot: refused */
    nrep = 0;
    CHECK(cr_proto_cmd(st, a, CR_ED_PPUT, args, na, put, 0) == 1 && rep[2] == 2, "PPUT to a factory slot not refused");
    CHECK(cr_preset_get(st->user, 64, &t) == 1 && t.p[CR_RES] == 99, "U1-1 read");
    args[0] = 64; args[1] = 0;
    nrep = 0;
    CHECK(cr_proto_cmd(st, b, CR_ED_SELECT, args, 2, put, 0) == 1 && b->cur == 64 && b->p.p[CR_RES] == 99, "SELECT on b");
    CHECK(cr_preset_get(st->user, 65, &t) == 0 && strcmp(t.name, "INIT") == 0, "an empty user slot isn't INIT");
    nrep = 0;
    CHECK(cr_proto_cmd(st, a, CR_ED_PRESET_RESET, args, 2, put, 0) == 1 && !cr_user_saved(st->user, 64), "RESET");

    /* a user bank through WAVE PUT / COMMIT / GET, and it plays */
    {
        cr_ubank_t ub;
        uint32_t c, i;
        memset(&ub, 0, sizeof ub);
        ub.magic = 0xC7; ub.ver = 1;
        strcpy(ub.name, "SQUARE");
        for (i = 0; i < 5u * CR_WT_N; i++)
            ub.w[i / CR_WT_N][i % CR_WT_N] = (int16_t)((i % CR_WT_N) < 128 ? 26000 : -26000);
        for (c = 0; c * 256u < sizeof ub; c++) {
            uint32_t n = sizeof ub - c * 256u < 256u ? sizeof ub - c * 256u : 256u;
            na = 0;
            args[na++] = 5; args[na++] = (uint8_t)c;
            pack((const uint8_t *)&ub + c * 256u, n, args, &na);
            nrep = 0;
            CHECK(cr_proto_cmd(st, a, CR_ED_WPUT, args, na, put, 0) == 1, "WPUT %u", c);
        }
        args[0] = 5;
        nrep = 0;
        CHECK(cr_proto_cmd(st, a, CR_ED_WCOMMIT, args, 1, put, 0) == 1 && rep[1] == 0, "WCOMMIT");
        CHECK(cr_user_bank(st->user, 5) && strcmp(cr_user_bank(st->user, 5)->name, "SQUARE") == 0, "bank 6 not stored");
        args[0] = 5; args[1] = 0;
        nrep = 0;
        CHECK(cr_proto_cmd(st, a, CR_ED_WGET, args, 2, put, 0) == 1 && nrep > 200, "WGET");
        cr_inst_load(b, 0);
        cr_inst_param(b, CR_W1BANK, cr_bank_val(CR_NBANK + 5u, 0));
        CHECK(b->tb[0] == CR_NBANK + 5u, "user bank 6 not playing: %u", b->tb[0]);
        midi3(b, 0x90, 48, 100);
        CHECK(level(b, 40) > 0.05f, "user bank 6 silent");
        cr_inst_all_off(b);
    }

    /* 0.12: the sub, two octaves under A4 (110 Hz): the same patch with and without it, the difference is the sub */
    {
        float la[256], lb[256];
        uint32_t w, blk, zc, k;
        float prev, pk;
        for (w = 0; w < 3u; w += 2u) {
            cr_inst_t *in[2] = {a, b};
            for (k = 0; k < 2u; k++) {
                cr_inst_init(in[k]);                  /* fresh: the same oscillator phases in both */
                cr_inst_set_user(in[k], st->user);
                cr_inst_param(in[k], CR_SUBLVL, k ? 0 : 127);
                cr_inst_param(in[k], CR_SUBOCT, 96);      /* -2 OCT */
                cr_inst_param(in[k], CR_SUBWAVE, w ? 106 : 0);
                cr_inst_transport(in[k], -1.0, 0.f, 0);
                midi3(in[k], 0x90, 69, 100);
            }
            zc = 0;
            pk = 0.f;
            prev = 0.f;
            for (blk = 0; blk < 345u; blk++) {       /* 1 s */
                cr_inst_render(a, la, 128);
                cr_inst_render(b, lb, 128);
                if (blk < 10u)
                    continue;
                for (k = 0; k < 128u; k++) {
                    float x = la[2 * k] - lb[2 * k];
                    pk = x > pk ? x : pk;
                    if (prev < 0.f && x >= 0.f)
                        zc++;
                    prev = x;
                }
            }
            cr_inst_all_off(a);
            cr_inst_all_off(b);
            printf("inst: the sub (%s, -2 OCT under A4): %.1f Hz, peak %.2f\n", w ? "square" : "sine",
                   (double)zc * 44100.0 / (335.0 * 128.0), (double)pk);
            CHECK(pk > 0.1f, "the sub is silent");
            CHECK(zc >= 104u && zc <= 108u, "the sub is at %u cycles, not about 106 (110 Hz in 0.97 s)", zc);
        }
    }

    /* 0.8: every value is a destination: the mod wheel moves Wave 1's bank and the arp's tempo */
    {
        float lr[256];
        cr_patch_t ap;
        uint32_t b0;
        cr_inst_load(a, 0);
        cr_inst_param(a, CR_DST5, CR_D_W1BANK);      /* MOD WHL -> BANK 1 */
        cr_inst_param(a, CR_DEP5, 127);
        cr_inst_param(a, CR_DST6, CR_D_TEMPO);       /* EXPR -> TEMPO */
        cr_inst_param(a, CR_DEP6, 127);
        midi3(a, 0xB0, 1, 0);
        midi3(a, 0xB0, 11, 0);
        cr_inst_render(a, lr, 128);
        cr_inst_render(a, lr, 128);
        b0 = a->tb[0];
        midi3(a, 0xB0, 1, 127);
        midi3(a, 0xB0, 11, 127);
        cr_inst_render(a, lr, 128);
        cr_inst_render(a, lr, 128);
        CHECK(a->tb[0] != b0 && a->tb[0] > b0, "the mod wheel didn't move bank 1 (%u -> %u)", b0, a->tb[0]);
        cr_synth_arp_view(&a->syn, &a->p, &ap);
        CHECK(ap.p[CR_TEMPO] > a->p.p[CR_TEMPO] + 40u, "expression didn't move the arp's tempo (%u, patch %u)",
              ap.p[CR_TEMPO], a->p.p[CR_TEMPO]);
        printf("inst: mod wheel moves bank 1 from %u to %u; expression the arp's tempo to %.0f BPM\n", b0, a->tb[0],
               (double)cr_arp_bpm(&ap));
    }


    /* 0.13: deferred bank builds (the Tracker's way) and a host's wavetable file in place of a bank */
    {
        static int16_t fw[5][CR_WT_N];
        uint32_t k, i;
        float lr[256];
        for (k = 0; k < 5u; k++)
            for (i = 0; i < CR_WT_N; i++)
                fw[k][i] = (int16_t)(i < 128u ? 20000 : -20000);
        cr_inst_load(a, 0);
        a->defer_tables = 1;
        cr_inst_param(a, CR_W1BANK, cr_bank_val(3, 0));
        CHECK(a->tb[0] != 3u && cr_inst_tables_pending(a), "deferred: bank 4 built at once (%u)", a->tb[0]);
        cr_inst_render(a, lr, 128);
        CHECK(a->tb[0] != 3u && cr_inst_tables_pending(a), "deferred: render built bank 4");
        cr_inst_tables(a);
        CHECK(a->tb[0] == 3u && !cr_inst_tables_pending(a), "deferred: cr_inst_tables didn't build bank 4");
        cr_inst_file_table(a, 0, (const int16_t (*)[CR_WT_N])fw);
        CHECK(a->tb[0] == 0xFEu && a->tab[0].w[0][0][10] > 10000 && a->tab[0].w[0][0][200] < -10000, "the file isn't playing");
        cr_inst_param(a, CR_W1BANK, cr_bank_val(5, 0));
        cr_inst_tables(a);
        CHECK(a->tb[0] == 0xFEu, "a bank change replaced the file");
        cr_inst_file_table(a, 0, 0);
        CHECK(a->tb[0] == 5u, "back from the file: bank %u, not 6", a->tb[0]);
        a->defer_tables = 0;
        cr_inst_param(a, CR_W2BANK, cr_bank_val(CR_BANK_NOISE, 1));
        CHECK(a->tb[1] == CR_BANK_NOISE && !cr_inst_tables_pending(a), "not deferred: noise bank not built at once");
        printf("inst: deferred bank builds and a wavetable file in place of a bank\n");
    }

    /* the arp-seq locked to the host: 120 BPM, 1/16 steps, the transport running from beat 0 */
    {
        uint32_t blk, last = 0xFFFF, steps = 0, first = 0;
        double ppq = 0.0;
        cr_inst_load(a, 0);
        cr_inst_param(a, CR_ARP, 127);
        cr_inst_param(a, CR_ARPRATE, 64);        /* (some division: count what it gives) */
        midi3(a, 0x90, 60, 100);
        midi3(a, 0x90, 64, 100);
        for (blk = 0; blk < 680u; blk++) {       /* just under 2 s: 4 beats (a 17th step would land on beat 4) */
            float lr[256];
            cr_inst_transport(a, ppq, 120.f, 1);
            cr_inst_render(a, lr, 128);
            if (a->arp.sounding >= 0 && (uint32_t)a->arp.sounding != last) {
                if (!steps)
                    first = blk;
                steps++;
            }
            last = a->arp.sounding >= 0 ? (uint32_t)a->arp.sounding : 0xFFFF;
            ppq += 128.0 * 120.0 / 60.0 / 44100.0;
        }
        notes = (int)steps;
        printf("inst: arp at 120 BPM from the host: %d notes in 4 beats, the first in block %u (%u ticks a step)\n", notes,
               first, cr_arp_ticks(&a->p));
        CHECK(first == 0, "the first arp step isn't on the downbeat");
        CHECK(notes == (int)(96u / cr_arp_ticks(&a->p)), "arp steps: %d, want %u", notes, 96u / cr_arp_ticks(&a->p));
    }
    free(a);
    free(b);
    free(st);
    if (fails)
        return 1;
    printf("ok   inst\n");
    return 0;
}
