/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth as an MPC plugin: the FM-1's engine (src/host/cr_inst.c) behind the community MPC toolkit's engine
 * interface (external/mpc-vst-plugins/wrapper/engine.h; its VST2 wrapper is what MPC loads). Parameter 0 is the
 * preset (also MPC's PRESET menu), then every patch value in the engine's order (params.json, gen_params.c).
 * User presets and wavetables come from "Crafty Synth.user" in the plugin's data folder: the plugin's file
 * (Mac) or the FM-1's user area, byte for byte. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "cr_inst.h"
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
} mpc_t;

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
    return m;
}
static void destroy(void *p) { free(p); }

static void midi(void *p, const uint8_t *msg, int len)
{
    if (len > 0 && msg[0] != 0xF0)
        cr_inst_midi(&((mpc_t *)p)->in, msg, (uint32_t)len);
}

/* the state: "CRS1" then the preset (4 hex), dirty (2), the patch (256 hex) */
static void state_get(mpc_t *m, char *b, int n)
{
    static const char H[] = "0123456789abcdef";
    const uint8_t *pt = (const uint8_t *)&m->in.p;
    int k = 0;
    uint32_t i;
    if (n < 4 + 6 + 2 * (int)CR_PATCH_BYTES + 1)
        return;
    memcpy(b, "CRS1", 4);
    k = 4;
    k += sprintf(b + k, "%04x%02x", m->in.cur, m->in.dirty);
    for (i = 0; i < CR_PATCH_BYTES; i++) {
        b[k++] = H[pt[i] >> 4];
        b[k++] = H[pt[i] & 15];
    }
    b[k] = 0;
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
    if (cr_inst_take_patch_changed(&m->in))
        m->report = 3 + CR_NP;                  /* a new patch: every value moved */
    if (m->report > 0) {
        int k = 3 + (int)CR_NP - m->report--;
        snprintf(key, (size_t)len, "%s", k < 3 ? PK[k] : KEYS[k - 3]);
        return 1;
    }
    if ((id = cr_inst_take_changed(&m->in)) == 0)
        return 0;
    snprintf(key, (size_t)len, "%s", KEYS[id - 1u]);
    return 1;
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, 0, song_pos};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
