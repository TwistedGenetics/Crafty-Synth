/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the shared WAV importer (src/host/cr_wav.c) and Random Patch (cr_patch_random). With a WAV file: its
 * bank (5 x 256 int16, little-endian) to out (tests/host/wav_ref.py checks it against the editor's algorithm); with
 * no arguments: Random Patch's rules. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cr_wav.h"
#include "cr_patch.h"
#include "cr_presets.h"
#include "cr_tables.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t rd(void *ctx, uint32_t off, void *buf, uint32_t n)
{
    FILE *f = ctx;
    if (fseek(f, (long)off, SEEK_SET))
        return 0;
    return (uint32_t)fread(buf, 1, n, f);
}
static int odd_saved(void *ctx, uint32_t slot) { (void)ctx; return slot & 1u; }

int main(int argc, char **argv)
{
    if (argc >= 3) {
        static cr_wav_work_t k;
        static int16_t bank[5][CR_WT_N];
        cr_wav_t w;
        FILE *f = fopen(argv[1], "rb"), *o;
        long size;
        if (!f)
            return 2;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        if (!cr_wav_open(&w, rd, f, (uint32_t)size)) {
            printf("wav: %s not read\n", argv[1]);
            return 3;
        }
        if (!cr_wav_bank(&w, 0, bank, &k))
            return 4;
        o = fopen(argv[2], "wb");
        fwrite(bank, 1, sizeof bank, o);
        fclose(o);
        fclose(f);
        printf("%u %u\n", (unsigned)w.frames, (unsigned)w.frame_size);
        return 0;
    }
    {
        char nm[14];
        uint32_t pk[5];
        cr_wav_name("Growl bass.v2.WAV", nm);
        CHECK(strcmp(nm, "GROWL BASS.V2") == 0, "name: %s", nm);
        cr_wav_name("très{}long name for a table.wav", nm);
        CHECK(strlen(nm) == 13u, "name length: %s", nm);
        cr_wav_picks(256, pk);
        CHECK(pk[0] == 0 && pk[1] == 64 && pk[2] == 128 && pk[3] == 191 && pk[4] == 255, "picks %u %u %u %u %u", pk[0], pk[1], pk[2], pk[3], pk[4]);
        cr_wav_picks(1, pk);
        CHECK(pk[4] == 0, "one frame's picks");
    }
    {
        uint32_t s = 12345, t, id, moved = 0, changed = 0, banks_factory = 1, slot;
        for (t = 0; t < 2000u; t++) {
            cr_patch_t p, cur, q;
            slot = cr_rand_slot(&s, odd_saved, 0);
            CHECK(slot < CR_NPRESET && (slot < CR_NFACTORY || (slot & 1u)), "slot %u is empty", slot);
            memcpy(&p, CR_FACTORY[t % CR_NFACTORY], sizeof p);
            (void)cr_patch_check(&p);
            q = p;
            cur = p;
            cur.p[CR_W1BANK] = 77;
            cur.p[CR_W2BANK] = 99;
            cr_patch_random(&p, &cur, &s, t & 1u ? CR_RAND_KEEP_TABLES : 0u);
            for (id = 0; id < CR_NP; id++) {
                if (cr_rand_keeps(id))
                    CHECK(p.p[id] == q.p[id], "kept value %u changed", id);
                CHECK(p.p[id] <= 127u, "value %u out of range", id);
                if (p.p[id] != q.p[id])
                    changed++;
                if (id >= CR_DST0 && id <= CR_DST7 && p.p[id] != q.p[id]) {
                    uint32_t d = p.p[id];
                    CHECK(d < CR_ND && !(d >= CR_D_AEGAMT && d <= CR_D_VOL) && d != CR_D_W1BANK && d != CR_D_W2BANK &&
                          cr_dest_ok(id - CR_DST0, d), "slot %u got destination %u", id - CR_DST0, d);
                }
            }
            if (t & 1u)
                CHECK(p.p[CR_W1BANK] == 77 && p.p[CR_W2BANK] == 99, "the banks weren't kept");
            else if (cr_bank_of(p.p[CR_W1BANK], 0) >= 8u || cr_bank_of(p.p[CR_W2BANK], 1) >= 8u)
                banks_factory = 0;
            if (p.p[CR_CUT] != q.p[CR_CUT])
                moved++;
            CHECK(memcmp(p.name, q.name, 12) == 0 && p.steps == q.steps, "the name or pattern changed");
        }
        CHECK(banks_factory, "a random bank wasn't a factory one");
        CHECK(moved > 1500u, "cutoff moved only %u times in 2000", moved);
        printf("random: %u values changed in 2000 patches (%.1f a patch), cutoff moved %u times\n", changed,
               (double)changed / 2000.0, moved);
    }
    {   /* the numbered wavetable files */
        static cr_tables_t t;
        static const char idx[] = "Basses/Growl.wav\r\nPads/Air.WAV\nGone.wav\nbasses/growl.wav\n";
        char out[256], lab[32];
        uint32_t n, a, s = 5;
        cr_tables_init(&t);
        cr_tables_load(&t, idx, sizeof idx - 1u);
        CHECK(t.count == 3u && t.saved == 3u, "index: %u numbers", (unsigned)t.count);
        cr_tables_begin(&t);
        CHECK(cr_tables_seen(&t, "basses/GROWL.wav") == 1u, "a known path got a new number");
        CHECK(cr_tables_seen(&t, "Pads/Air.WAV") == 2u, "Air");
        CHECK(cr_tables_seen(&t, "Basses/Acid.wav") == 4u, "a new path: %u", (unsigned)cr_tables_find(&t, "Basses/Acid.wav"));
        CHECK(cr_tables_seen(&t, "Top.wav") == 5u, "Top");
        cr_tables_done(&t);
        CHECK(!cr_tables_present(&t, 3) && cr_tables_present(&t, 4), "present");
        /* by name: Basses/Acid, Basses/Growl, Pads/Air, Top */
        CHECK(cr_tables_at(&t, 0) == 4u && cr_tables_at(&t, 1) == 1u && cr_tables_at(&t, 2) == 2u && cr_tables_at(&t, 3) == 5u,
              "sorted %u %u %u %u", cr_tables_at(&t, 0), cr_tables_at(&t, 1), cr_tables_at(&t, 2), cr_tables_at(&t, 3));
        CHECK(cr_tables_step(&t, 0, 1, 0) == 4u && cr_tables_step(&t, 4, -1, 0) == 0u && cr_tables_step(&t, 5, 3, 0) == 5u, "step");
        CHECK(cr_tables_step(&t, 4, 1, 1) == 2u && cr_tables_step(&t, 2, 1, 1) == 5u && cr_tables_step(&t, 1, -1, 1) == 4u &&
              cr_tables_step(&t, 4, -1, 1) == 5u - 5u, "step by folder");
        n = cr_tables_new_lines(&t, out, sizeof out);
        out[n] = 0;
        CHECK(!strcmp(out, "Basses/Acid.wav\r\nTop.wav\r\n") && cr_tables_new_lines(&t, out, sizeof out) == 0u, "new lines: %s", out);
        cr_tables_label(&t, 1, lab, sizeof lab);
        CHECK(!strcmp(lab, "Growl"), "label %s", lab);
        for (n = 0; n < 50u; n++) {
            a = cr_tables_random(&t, &s);
            CHECK(cr_tables_present(&t, a), "random %u", a);
        }
    }
    if (fails)
        return 1;
    printf("ok   wav and random\n");
    return 0;
}
