/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth as an MPC plugin: the FM-1's engine (src/host/cr_inst.c) behind the community MPC toolkit's engine
 * interface (external/mpc-vst-plugins/wrapper/engine.h; its VST2 wrapper is what MPC loads). Parameter 0 is the
 * preset (also MPC's PRESET menu), then every patch value in the engine's order (params.json, gen_params.c).
 * User presets and wavetables come from "Crafty Synth.user" in the plugin's data folder: the plugin's file
 * (Mac) or the FM-1's user area, byte for byte. 0.13: WAVs under its Wavetables folder are numbered in
 * TableIndex.txt (src/host/cr_tables.c, the Twisted Tracker's way) and "table1" / "table2" play one in place of a
 * wave's bank; "random" is Random Patch (src/core cr_patch_random). Copyright (C) 2026 Jamie (TwistedGenetics). */
#define _DEFAULT_SOURCE                         /* strdup, strcasecmp, dirent */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "engine.h"
#include "cr_inst.h"
#include "cr_tables.h"
#include "cr_wav.h"
#include "cr_keys.h"
#include "cr_mpc_lists.h"

#define NKEY (CR_NP)
static mpc_list_t LISTS[CR_NP];
static char KEYS[CR_NP][16];
static int ready;

typedef struct {
    cr_inst_t in;
    uint8_t user[CR_USER_BYTES];
    float buf[2 * CR_INST_FRAMES];
    int report;                                 /* after a new patch: the keys still to report (mpc_engine_changed) */
    uint32_t file[2];                           /* the wavetable files the waves play (numbers, 0: the bank) */
    char fpath[2][CR_TABLES_PATH];              /*   ... their paths (kept when missing: the project finds them later) */
    int16_t waves[2][5][CR_WT_N];
    int rkeep;                                  /* RANDOM keeps the tables */
    int shown[2];                               /* the 3D views' bank as last reported (w1shown, w2shown; -1: not yet) */
    uint32_t seed;
} mpc_t;

/* ---- the wavetable files: one numbered list for every instance (the data folder's Wavetables) ---- */
static cr_tables_t TABS;
static char TDIR[1024];
static int tabs_ready;
static int tb_namecmp(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }
static void tabs_walk(const char *rel, int depth)
{
    char path[1400], sub[CR_TABLES_PATH];
    char *names[512];
    int n = 0, i;
    DIR *d;
    struct dirent *e;
    snprintf(path, sizeof path, rel[0] ? "%s/%s" : "%s", TDIR, rel);
    if (depth > 8 || !(d = opendir(path)))
        return;
    while ((e = readdir(d)) != 0 && n < 512)
        if (e->d_name[0] != '.')
            names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], tb_namecmp);  /* new files get their numbers in name order */
    for (i = 0; i < n; i++) {
        struct stat st;
        size_t l = strlen(names[i]);
        snprintf(sub, sizeof sub, rel[0] ? "%s/%s" : "%s%s", rel, names[i]);
        snprintf(path, sizeof path, "%s/%s", TDIR, sub);
        if (!stat(path, &st) && S_ISDIR(st.st_mode))
            tabs_walk(sub, depth + 1);
        else if (l > 4 && !strcasecmp(names[i] + l - 4, ".wav"))
            cr_tables_seen(&TABS, sub);
        free(names[i]);
    }
}
static void tabs_scan(const char *dir)
{
    char index[1100], buf[4096];
    FILE *f;
    uint32_t n;
    if (!dir)
        return;
    snprintf(TDIR, sizeof TDIR, "%s/Wavetables", dir);
    mkdir(TDIR, 0755);
    snprintf(index, sizeof index, "%s/TableIndex.txt", dir);
    if (!tabs_ready) {
        cr_tables_init(&TABS);
        if ((f = fopen(index, "rb")) != 0) {
            char *t = malloc(1u << 20);
            if (t) {
                n = (uint32_t)fread(t, 1, 1u << 20, f);
                cr_tables_load(&TABS, t, n);
                free(t);
            }
            fclose(f);
        }
        tabs_ready = 1;
    }
    cr_tables_begin(&TABS);
    tabs_walk("", 0);
    cr_tables_done(&TABS);
    if ((n = cr_tables_new_lines(&TABS, buf, sizeof buf)) > 0 && (f = fopen(index, "ab")) != 0) {
        do
            fwrite(buf, 1, n, f);
        while ((n = cr_tables_new_lines(&TABS, buf, sizeof buf)) > 0);
        fclose(f);
    }
}
static uint32_t file_read(void *ctx, uint32_t off, void *buf, uint32_t n)
{
    FILE *f = ctx;
    if (fseek(f, (long)off, SEEK_SET))
        return 0;
    return (uint32_t)fread(buf, 1, n, f);
}
/* wave k plays file id (0: its bank); the path is kept (a missing file plays the bank) */
static void set_file(mpc_t *m, int k, uint32_t id)
{
    static cr_wav_work_t work;
    char path[1400];
    const char *rel = cr_tables_path(&TABS, id);
    FILE *f;
    cr_wav_t w;
    int ok = 0;
    m->file[k] = id;                            /* kept as set (a number not on this MPC shows MISSING) */
    snprintf(m->fpath[k], sizeof m->fpath[k], "%s", rel ? rel : "");
    if (rel && cr_tables_present(&TABS, id)) {
        snprintf(path, sizeof path, "%s/%s", TDIR, rel);
        if ((f = fopen(path, "rb")) != 0) {
            fseek(f, 0, SEEK_END);
            ok = cr_wav_open(&w, file_read, f, (uint32_t)ftell(f)) && cr_wav_bank(&w, 0, m->waves[k], &work);
            fclose(f);
        }
    }
    cr_inst_file_table(&m->in, (uint32_t)k, ok ? (const int16_t (*)[CR_WT_N])m->waves[k] : 0);
}
/* what wave k's 3D view shows (the skin's w<k>shown): 0 a user bank or a wavetable file, else factory bank + 1 */
static int shown_of(const mpc_t *m, int k)
{
    uint32_t b = cr_bank_of(m->in.p.p[k ? CR_W2BANK : CR_W1BANK], k);
    return m->in.file[k] || b >= CR_NBANK ? 0 : (int)b + 1;
}
static int saved_slot(void *ctx, uint32_t slot) { return cr_user_saved(((mpc_t *)ctx)->user, slot); }
static void random_patch(mpc_t *m)
{
    cr_patch_t t;
    uint32_t k;
    if (!m->seed)
        m->seed = (uint32_t)time(0) * 2654435761u + (uint32_t)(uintptr_t)m;
    (void)cr_preset_get(m->user, cr_rand_slot(&m->seed, saved_slot, m), &t);
    cr_patch_random(&t, &m->in.p, &m->seed, m->rkeep ? CR_RAND_KEEP_TABLES : 0u);
    cr_inst_all_off(&m->in);
    cr_inst_use(&m->in, &t);
    m->in.dirty = 1;
    if (!m->rkeep)                              /* as the Tracker: half the time one of your files, else the bank */
        for (k = 0; k < 2u; k++)
            set_file(m, (int)k, cr_rand(&m->seed) % 2u ? cr_tables_random(&TABS, &m->seed) : 0u);
    m->report = 3 + CR_NP + 2;                  /* every value moved */
}

static void tables(void)
{
    uint32_t i;
    if (ready)
        return;
    for (i = 0; i < CR_NP; i++) {
        mpc_list(i, &LISTS[i]);
        mpc_key(CR_KEY[i], KEYS[i]);
    }
    ready = 1;
}
static int param_of(const char *key)
{
    uint32_t i;
    for (i = 0; i < CR_NP; i++)
        if (!strcmp(KEYS[i], key))
            return (int)i;
    return -1;
}
static const char *uname(void *ctx, uint32_t u)
{
    const cr_ubank_t *b = cr_user_bank(((mpc_t *)ctx)->user, u);
    return b ? b->name : "";                    /* empty: "USER 3", the list's own name */
}

static void *create(const char *dir)
{
    mpc_t *m = calloc(1, sizeof *m);
    char path[1024];
    FILE *f;
    if (!m)
        return 0;
    tables();
    memset(m->user, 0xFF, sizeof m->user);
    if (dir) {
        snprintf(path, sizeof path, "%s/Crafty Synth.user", dir);
        if ((f = fopen(path, "rb")) != 0) {
            if (fread(m->user, 1, sizeof m->user, f) != sizeof m->user)
                memset(m->user, 0xFF, sizeof m->user);
            fclose(f);
        }
    }
    cr_inst_init(&m->in);
    cr_inst_set_user(&m->in, m->user);
    (void)cr_inst_take_patch_changed(&m->in);
    tabs_scan(dir);
    m->shown[0] = m->shown[1] = -1;
    return m;
}
static void destroy(void *p) { free(p); }

static void midi(void *p, const uint8_t *msg, int len)
{
    if (len > 0 && msg[0] != 0xF0)
        cr_inst_midi(&((mpc_t *)p)->in, msg, (uint32_t)len);
}

/* the state: "CRS1" then the preset (4 hex), dirty (2), the patch (256 hex); 0.13: "|" and each wave's wavetable
 * file's path ("" = the bank), "|" between */
static void state_get(mpc_t *m, char *b, int n)
{
    static const char H[] = "0123456789abcdef";
    const uint8_t *pt = (const uint8_t *)&m->in.p;
    int k = 0;
    uint32_t i;
    if (n < 4 + 6 + 2 * (int)CR_PATCH_BYTES + 3 + 2 * (int)CR_TABLES_PATH)
        return;
    memcpy(b, "CRS1", 4);
    k = 4;
    k += sprintf(b + k, "%04x%02x", m->in.cur, m->in.dirty);
    for (i = 0; i < CR_PATCH_BYTES; i++) {
        b[k++] = H[pt[i] >> 4];
        b[k++] = H[pt[i] & 15];
    }
    b[k] = 0;
    if (m->fpath[0][0] || m->fpath[1][0])
        sprintf(b + k, "|%s|%s", m->fpath[0], m->fpath[1]);
}
static int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
static void state_set(mpc_t *m, const char *s)
{
    cr_patch_t pt;
    uint8_t *b = (uint8_t *)&pt;
    unsigned cur = 0, dirty = 0;
    uint32_t i;
    if (strlen(s) < 10 + 2 * CR_PATCH_BYTES || memcmp(s, "CRS1", 4) || sscanf(s + 4, "%4x%2x", &cur, &dirty) != 2)
        return;
    for (i = 0; i < CR_PATCH_BYTES; i++) {
        int a = hex(s[10 + 2 * i]), c = hex(s[11 + 2 * i]);
        if (a < 0 || c < 0)
            return;
        b[i] = (uint8_t)(a << 4 | c);
    }
    if (cr_patch_check(&pt))
        return;
    cr_inst_all_off(&m->in);
    cr_inst_use(&m->in, &pt);
    m->in.cur = (uint16_t)(cur % CR_NPRESET);
    m->in.dirty = (uint8_t)(dirty != 0);
    (void)cr_inst_take_patch_changed(&m->in);
    {   /* 0.13: the wavetable files, by path (their numbers may differ on another MPC) */
        const char *p = s + 10 + 2 * CR_PATCH_BYTES;
        int k;
        for (k = 0; k < 2; k++) {
            char rel[CR_TABLES_PATH];
            size_t l = 0;
            if (*p == '|')
                for (p++; *p && *p != '|' && l < sizeof rel - 1u; p++)
                    rel[l++] = *p;
            rel[l] = 0;
            if (l) {
                uint32_t id = cr_tables_find(&TABS, rel);
                set_file(m, k, id);
                if (!id)                        /* not in this index: kept, plays the bank */
                    snprintf(m->fpath[k], sizeof m->fpath[k], "%s", rel);
            } else if (m->file[k] || m->fpath[k][0])
                set_file(m, k, 0);
        }
    }
}

static void set_param(void *p, const char *key, const char *val)
{
    mpc_t *m = p;
    int id;
    if (!strcmp(key, "state")) {
        state_set(m, val);
        return;
    }
    if (!strcmp(key, "pbank") || !strcmp(key, "pslot")) {   /* bank 0..23 x slot 1..8 */
        int v = (int)(atof(val) + 0.5), b = m->in.cur / 8, s = m->in.cur % 8;
        if (key[1] == 'b')
            b = v < 0 ? 0 : v > (int)CR_NPRESET / 8 - 1 ? (int)CR_NPRESET / 8 - 1 : v;
        else
            s = v < 1 ? 0 : v > 8 ? 7 : v - 1;
        if (b * 8 + s != m->in.cur) {
            cr_inst_all_off(&m->in);
            cr_inst_load(&m->in, (uint32_t)(b * 8 + s));   /* reported by mpc_engine_changed */
        }
        return;
    }
    if (!strcmp(key, "table1") || !strcmp(key, "table2")) {
        int v = (int)(atof(val) + 0.5), k = key[5] - '1';
        if (v < 0 || v > (int)CR_TABLES_MAX)
            return;
        if ((uint32_t)v != m->file[k])
            set_file(m, k, (uint32_t)v);
        return;
    }
    if (!strcmp(key, "random")) {               /* a trigger: it springs back to 0 */
        if (atof(val) > 0.5)
            random_patch(m);
        return;
    }
    if (!strcmp(key, "rkeep")) {
        m->rkeep = atoi(val) == 1;
        return;
    }
    if (!strcmp(key, "preset")) {
        int n = (int)(atof(val) + 0.5);
        if (n >= 0 && n < (int)CR_NPRESET && n != m->in.cur) {
            cr_inst_all_off(&m->in);
            cr_inst_load(&m->in, (uint32_t)n);  /* reported by mpc_engine_changed */
        }
        return;
    }
    if ((id = param_of(key)) < 0)
        return;
    if (LISTS[id].n) {
        int i = atoi(val);
        if (i >= 0 && i < LISTS[id].n)
            cr_inst_param(&m->in, (uint32_t)id, LISTS[id].rep[i]);
    } else {
        int v = (int)(atof(val) + 0.5);         /* a knob: 0..127, rounded */
        cr_inst_param(&m->in, (uint32_t)id, (uint32_t)(v < 0 ? 0 : v > 127 ? 127 : v));
    }
}

static int get_param(void *p, const char *key, char *buf, int n)
{
    mpc_t *m = p;
    const char *us;
    int id;
    if (!strcmp(key, "state")) {
        state_get(m, buf, n);
        return (int)strlen(buf);
    }
    if (!strcmp(key, "preset"))
        return snprintf(buf, (size_t)n, "%d", m->in.cur);
    if (!strcmp(key, "table1") || !strcmp(key, "table2"))
        return snprintf(buf, (size_t)n, "%u", (unsigned)m->file[key[5] - '1']);
    if (!strcmp(key, "table1_display") || !strcmp(key, "table2_display")) {   /* "GROWL", "BANK", "MISSING" */
        int k = key[5] - '1';
        char lab[64];
        if (!m->file[k] && !m->fpath[k][0])
            return snprintf(buf, (size_t)n, "BANK");
        if (!m->file[k] || !cr_tables_present(&TABS, m->file[k]))
            return snprintf(buf, (size_t)n, "MISSING");
        cr_tables_label(&TABS, m->file[k], lab, sizeof lab);
        return snprintf(buf, (size_t)n, "%s", lab);
    }
    if (!strcmp(key, "random"))
        return snprintf(buf, (size_t)n, "0");
    if (!strcmp(key, "w1shown") || !strcmp(key, "w2shown"))
        return snprintf(buf, (size_t)n, "%d", shown_of(m, key[1] - '1'));
    if (!strcmp(key, "rkeep"))
        return snprintf(buf, (size_t)n, "%d", m->rkeep);
    if (!strcmp(key, "pbank"))
        return snprintf(buf, (size_t)n, "%d", m->in.cur / 8);
    if (!strcmp(key, "pslot"))
        return snprintf(buf, (size_t)n, "%d", m->in.cur % 8 + 1);
    if (!strcmp(key, "pbank_display")) {        /* "3" (factory bank 3), "U12" (user bank 12) */
        int b = m->in.cur / 8;
        return b < 8 ? snprintf(buf, (size_t)n, "%d", b + 1) : snprintf(buf, (size_t)n, "U%d", b - 7);
    }
    if (!strncmp(key, "preset:", 7) || !strcmp(key, "preset_display")) {   /* "U1-1 MY SAW" */
        int s = key[6] == ':' ? atoi(key + 7) : m->in.cur;
        char lab[8];
        cr_patch_t pt;
        int real;
        if (s < 0 || s >= (int)CR_NPRESET)
            return 0;
        cr_preset_label((uint32_t)s, lab);
        real = cr_preset_get(m->user, (uint32_t)s, &pt);
        return snprintf(buf, (size_t)n, "%s %s", lab, real ? pt.name : "(empty)");
    }
    us = strstr(key, "_display");
    if (us && !us[8]) {                         /* a value as the FM-1 shows it */
        char k[24];
        size_t kl = (size_t)(us - key);
        if (kl >= sizeof k)
            return 0;
        memcpy(k, key, kl);
        k[kl] = 0;
        if ((id = param_of(k)) < 0)
            return 0;
        cr_param_text(&m->in.p, (uint32_t)id, buf, 1, uname, m);
        return (int)strlen(buf);
    }
    if ((id = param_of(key)) < 0)
        return 0;
    if (LISTS[id].n)
        return snprintf(buf, (size_t)n, "%d", LISTS[id].of[m->in.p.p[id]]);
    return snprintf(buf, (size_t)n, "%d", m->in.p.p[id]);
}

static void render(void *p, int16_t *out, int frames)
{
    mpc_t *m = p;
    int i;
    if (frames > (int)CR_INST_FRAMES)
        frames = (int)CR_INST_FRAMES;
    cr_inst_render(&m->in, m->buf, (uint32_t)frames);
    for (i = 0; i < 2 * frames; i++) {
        float x = m->buf[i] * 32767.f;
        out[i] = (int16_t)(x > 32767.f ? 32767 : x < -32768.f ? -32768 : (int)x);
    }
}

static void song_pos(void *p, double ppq, double bpm, int playing, int frames)
{
    (void)frames;
    cr_inst_transport(&((mpc_t *)p)->in, ppq, (float)bpm, playing);
}

/* a parameter changed from inside (a MIDI CC on the track, a program change): the MPC's screen follows */
int mpc_engine_changed(void *p, char *key, int len)
{
    static const char *const PK[3] = {"preset", "pbank", "pslot"};
    mpc_t *m = p;
    uint32_t id;
    int k;
    if (cr_inst_take_patch_changed(&m->in))
        m->report = 3 + CR_NP + 2;              /* a new patch: every value moved */
    if (m->report > 0) {                        /* (after RANDOM, the two table files too) */
        int k = 3 + (int)CR_NP + 2 - m->report--;
        snprintf(key, (size_t)len, "%s", k < 3 ? PK[k] : k < 3 + (int)CR_NP ? KEYS[k - 3] : k == 3 + (int)CR_NP ? "table1" : "table2");
        return 1;
    }
    for (k = 0; k < 2; k++) {                   /* the 3D views follow the bank (or file) that plays */
        int s = shown_of(m, k);
        if (s != m->shown[k]) {
            m->shown[k] = s;
            snprintf(key, (size_t)len, "w%dshown", k + 1);
            return 1;
        }
    }
    if ((id = cr_inst_take_changed(&m->in)) == 0)
        return 0;
    snprintf(key, (size_t)len, "%s", KEYS[id - 1u]);
    return 1;
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, 0, song_pos};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
