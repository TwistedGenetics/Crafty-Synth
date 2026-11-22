/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft on the FM-1: the panel, the screen, the presets and user wavetables in flash, MIDI in and out.
 * A unity-build fragment: on the FM-1 src/hardware/cr_main_fm1.c includes it after X0X's platform (gfx.c, plat.h)
 * and defines cr_lock() / cr_unlock() / cr_wire(); on the host tests/host/cr_host.c does the same with stubs.
 *
 * Who runs what: cr_app_render, cr_app_midi and cr_app_realtime run in the audio interrupt; everything else in the
 * main loop. The main loop changes what the interrupt reads (the patch, the synth, the arp) only inside cr_lock().
 * A bank change or a preset asked for over MIDI (in the interrupt) is only flagged; the main loop builds the
 * wavetables (a few ms) and loads the preset.
 *
 * The panel (docs/MANUAL.md has the whole of it):
 *   keys       play (chromatic from F, or the scale); SEQ held: enter pattern steps
 *   OCT- OCT+  the keys' octave (both held 5 s: the FM-1's update mode, as in every firmware)
 *   PRESET     the preset, 1-1 .. 8-8 (it loads as you turn)    SELECT  the page      ALGO  the osc modifier
 *   K1..K4     the page's four values
 *   HOME       OSC -> WAVES -> TUNE        EDIT  FILTER          ENV  FILTER EG -> AMP EG -> MOD EG
 *   LFO        LFO 1 -> LFO 2; held while turning a knob: LFO assign (the CRAFTsynth's hold-LFO)
 *   SEL        MATRIX; held while turning a knob: MOD EG assign
 *   FX         DIST + DELAY -> OUTPUT      ARP  arp on / off (and its page)     SEQ  the pattern page
 *   REC        LATCH         PLAY  all notes off (on SAVE: INIT the patch)      GLO  global pages      SAVE  save
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_app.h"
#include "cr_presets.h"
#include "cr_user.h"
#include "cr_text.h"
#include "cr_font.h"
#include "cr_math.h"

#ifndef CR_VERSION
#define CR_VERSION "DEV"
#endif

/* ------------------------------------------------------------------------------------------------- state --- */
static cr_patch_t cr_p;                          /* the working patch: what sounds */
static cr_globals_t cr_g;
static cr_synth_t cr_syn;
static cr_arp_t cr_arp;
static int16_t cr_mem[CR_SYN_MEM] X0X_POOL;      /* the synth's memory (delay, reverb, chorus, comb): 144 KB */
static cr_wtbank_t cr_tab[3] X0X_POOL;           /* banks ready to play: Wave 1's, Wave 2's, one being built */
static uint8_t cr_tab_bank[3] = {0xFF, 0xFF, 0xFF};
/* the user area in flash: src/core/cr_user.h */
_Static_assert(CR_FL_BANKS + CR_NUBANK * CR_SECTOR <= CR_FLASH_SIZE, "the user area");
static cr_ubank_t cr_ubw X0X_POOL;               /* a bank coming in from the editor */
static uint8_t cr_sec[CR_SECTOR] X0X_POOL;       /* a sector being rewritten */
static const cr_ubank_t *cr_ubank(uint32_t u)    /* user bank u in flash, or 0 (empty) */
{
    const cr_ubank_t *b = (const cr_ubank_t *)cr_flash_ptr(CR_FL_BANKS + u * CR_SECTOR);
    return u < CR_NUBANK && b->magic == CR_UBANK_MAGIC && b->ver == 1u ? b : 0;
}
static float cr_fbuf[2u * 128u];
static uint8_t cr_dirty;                         /* the working patch differs from its preset */
static volatile uint8_t cr_want_tables = 1, cr_want_draw;
static volatile int16_t cr_want_preset = -1;     /* a program change (from the interrupt) */
static volatile uint32_t cr_clock_n, cr_clock_t0, cr_clock_ms;
static volatile uint8_t cr_clock_seen;
static volatile float cr_clock_bpm;

enum { PG_OSC, PG_WAVES, PG_OSCB, PG_TUNE, PG_FILTER, PG_FEG, PG_AEG, PG_MEG, PG_LFO1, PG_LFO2, PG_MATRIX, PG_FX,
       PG_OUT, PG_SPACE, PG_SPACE2, PG_COLOUR, PG_CRUSH, PG_ARP, PG_SEQ, PG_GLOB, PG_GLOB2, PG_GLOB3, PG_SAVE, PG_N };
static const char *const CR_PG_NAME[PG_N] = {"OSC", "WAVES", "OSC B", "TUNE", "FILTER", "FILTER EG", "AMP EG",
    "MOD EG", "LFO 1", "LFO 2", "MATRIX", "FX", "OUTPUT", "REVERB", "REVERB 2", "MOD FX", "CRUSH", "ARP", "SEQ",
    "MIDI", "KEYS", "SETUP", "SAVE"};
/* the page's four knobs: a patch parameter, or one of the G_ values (globals and page controls) */
enum { G_NONE = 100, G_MSRC, G_MDST, G_MDEP, G_STEPS, G_MCH, G_MIN, G_CLOCK, G_BEND, G_KEYS, G_SCALE, G_ROOT,
       G_VELO, G_LOCAL, G_BREATH, G_ARPOUT, G_SLOT, G_CPOS, G_CHAR, G_THEME };
static const uint8_t CR_PAGE_P[PG_N][4] = {
    {CR_W1POS, CR_W2POS, CR_MIX, CR_OMAMT},   {CR_W1BANK, CR_W2BANK, CR_OMMODE, CR_SPREAD},
    {CR_BMODE, CR_BAMT, CR_W2POS, CR_TUNE},   {CR_TUNE, CR_FINE, CR_OCT, CR_GLIDE},
    {CR_CUT, CR_RES, CR_MORPH, CR_FTYPE},
    {CR_FA, CR_FD, CR_FS, CR_FR},             {CR_AA, CR_AD, CR_AS, CR_AR},
    {CR_MA, CR_MD, CR_MS, CR_MR},             {CR_L1RATE, CR_L1SHAPE, CR_L1MODE, CR_L1SYNC},
    {CR_L2RATE, CR_L2SHAPE, CR_L2MODE, CR_L2SYNC}, {G_MSRC, G_MDST, G_MDEP, G_NONE},
    {CR_DIST, CR_DMIX, CR_DTIME, CR_DFB},     {CR_DSYNC, CR_VOL, CR_AEGAMT, CR_FEGAMT},
    {CR_VMODE, CR_VSIZE, CR_VDECAY, CR_VMIX}, {CR_VWARP, CR_VDENS, CR_VMOD, CR_VMIX},
    {CR_CHMIX, CR_CHRATE, CR_PHMIX, CR_PHRATE}, {CR_ERA, CR_CRUSH, CR_OTT, CR_DIST},
    {CR_ARP, CR_TEMPO, CR_ARPRATE, G_STEPS},  {G_STEPS, CR_TEMPO, CR_ARPRATE, CR_ARP},
    {G_MCH, G_MIN, G_CLOCK, G_BEND},          {G_KEYS, G_SCALE, G_ROOT, G_VELO},
    {G_LOCAL, G_BREATH, G_ARPOUT, G_THEME},    {G_SLOT, G_NONE, G_CPOS, G_CHAR},
};
static uint32_t cr_page = PG_OSC;
static uint32_t cr_ver = 1, cr_drawn;            /* the screen redraws when cr_ver moves on */
static char cr_msg[28];
static uint32_t cr_msg_until;
static int32_t cr_hit = -1;                      /* the knob turned last: its value shown big */
static uint32_t cr_hit_p, cr_hit_until;
static uint32_t cr_keys_prev, cr_btn_prev, cr_btn_used, cr_keys_down;
static int32_t cr_oct;                           /* the keys' octave, -3..3 */
static uint8_t cr_latch, cr_lat_on[16], cr_lat_n; /* LATCH: the notes kept sounding */
static uint32_t cr_msrc;                         /* MATRIX: the slot (source) shown */
static uint32_t cr_save_slot, cr_cpos;
static uint8_t cr_sus;                           /* MIDI sustain pedal */
static uint32_t cr_sus_bits[4];
static uint8_t cr_key_note[27];                  /* the note each key started (so OCT moves don't hang notes) */

typedef struct {
    const char *name;
    uint16_t bg, panel, bar, ink, soft, acc, acc2, track, hil, line;
} cr_theme_t;
#define CR_NTHEME 6u
static const cr_theme_t CR_THEMES[CR_NTHEME] = {
    /* CRAFT: the CRAFTsynth 2.0's own: white plastic, black print, its blue (SHIFT) and grey (PRESET) */
    {"CRAFT", RGB(226, 226, 222), RGB(247, 247, 245), RGB(206, 206, 201), RGB(14, 15, 17), RGB(92, 98, 106),
     RGB(22, 101, 216), RGB(40, 44, 50), RGB(200, 200, 195), RGB(206, 224, 250), RGB(182, 182, 177)},
    /* CRAFT NIGHT: the same colours on a dark body */
    {"CRAFT NIGHT", RGB(16, 18, 21), RGB(30, 33, 38), RGB(44, 48, 55), RGB(244, 244, 242), RGB(160, 166, 174),
     RGB(64, 140, 255), RGB(214, 217, 222), RGB(60, 65, 73), RGB(22, 52, 96), RGB(56, 61, 68)},
    /* SERUM: slate panels, its cyan-blue tables */
    {"SERUM", RGB(20, 23, 27), RGB(34, 39, 45), RGB(46, 52, 60), RGB(232, 236, 239), RGB(140, 150, 161),
     RGB(63, 169, 245), RGB(126, 222, 200), RGB(58, 64, 72), RGB(24, 58, 86), RGB(52, 58, 66)},
    /* ACID: light, an acid green and a hot magenta (jungle flyers) */
    {"ACID", RGB(238, 240, 230), RGB(250, 251, 245), RGB(214, 218, 200), RGB(12, 14, 10), RGB(80, 86, 72),
     RGB(40, 128, 0), RGB(200, 0, 110), RGB(202, 206, 190), RGB(220, 245, 170), RGB(188, 192, 176)},
    /* AMBER: a retro CRT, amber on near black */
    {"AMBER", RGB(14, 10, 6), RGB(28, 20, 12), RGB(46, 32, 16), RGB(255, 196, 110), RGB(190, 130, 60),
     RGB(255, 160, 40), RGB(255, 226, 170), RGB(64, 44, 22), RGB(80, 48, 10), RGB(60, 42, 22)},
    /* PAPER: the plainest and highest contrast: white, black, one strong blue */
    {"PAPER", RGB(255, 255, 255), RGB(255, 255, 255), RGB(230, 230, 230), RGB(0, 0, 0), RGB(50, 50, 50),
     RGB(0, 70, 220), RGB(0, 0, 0), RGB(190, 190, 190), RGB(255, 220, 60), RGB(140, 140, 140)},
};
static void cr_touch(void) { cr_ver++; }
static void cr_flash(const char *m)
{
    uint32_t i;
    for (i = 0; m[i] && i < sizeof cr_msg - 1u; i++)
        cr_msg[i] = m[i];
    cr_msg[i] = 0;
    cr_msg_until = plat_ms() + 1400u;
    cr_touch();
}
static int32_t lim32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static void cr_cpy(char *b, const char *s)
{
    while ((*b++ = *s++))
        ;
}
static uint32_t cr_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void cr_num(char *b, int32_t v, int sign)
{
    char t[12];
    uint32_t n = 0, k = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0)
        b[k++] = '-';
    else if (sign && v > 0)
        b[k++] = '+';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        b[k++] = t[--n];
    b[k] = 0;
}
static void cr_cat(char *b, const char *s) { cr_cpy(b + cr_len(b), s); }
static void cr_dec(char *b, float v, uint32_t places)   /* v with 1 or 2 decimals, no sign */
{
    uint32_t sc = places == 2u ? 100u : 10u, t = (uint32_t)(v * (float)sc + 0.5f), k;
    cr_num(b, (int32_t)(t / sc), 0);
    k = cr_len(b);
    b[k++] = '.';
    if (places == 2u)
        b[k++] = (char)('0' + t / 10u % 10u);
    b[k++] = (char)('0' + t % 10u);
    b[k] = 0;
}

/* -------------------------------------------------------------------------------------------- MIDI out --- */
static void cr_midi3(uint32_t st, uint32_t d1, uint32_t d2)
{
    plat_midi_out((st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}

/* ------------------------------------------------------------------------------------------- wavetables --- */
static const int16_t (*cr_bank_base(uint32_t b))[CR_WT_N]
{
    if (b >= CR_NBANK) {
        const cr_ubank_t *u = cr_ubank(b - CR_NBANK);
        return u ? (const int16_t (*)[CR_WT_N])u->w : cr_wt_factory(0);
    }
    return cr_wt_factory(b);
}
/* the banks the patch names, built if they aren't (main loop). Never into a table the synth is reading: with
 * three, there is always one free, or (both banks new) the synth reads the first one built while the second builds */
static void cr_tables(void)
{
    uint32_t want[2], i, k;
    int32_t at[2] = {-1, -1};
    want[0] = cr_bank_of(cr_p.p[CR_W1BANK], 0);
    want[1] = cr_bank_of(cr_p.p[CR_W2BANK], 1);
    for (k = 0; k < 2u; k++)
        for (i = 0; i < 3u; i++)
            if (cr_tab_bank[i] == want[k])
                at[k] = (int32_t)i;
    for (k = 0; k < 2u; k++) {
        if (at[k] >= 0)
            continue;
        for (i = 0; i < 3u; i++)
            if ((int32_t)i != at[k ^ 1u] && cr_syn.t1 != &cr_tab[i] && cr_syn.t2 != &cr_tab[i])
                break;
        if (i == 3u) {                           /* both read tables are in use: read the other wave's for now */
            cr_lock();
            cr_synth_tables(&cr_syn, &cr_tab[at[k ^ 1u]], &cr_tab[at[k ^ 1u]], 0);
            cr_unlock();
            for (i = 0; i < 3u && (int32_t)i == at[k ^ 1u]; i++)
                ;
        }
        cr_tab_bank[i] = 0xFF;
        cr_wt_build(&cr_tab[i], cr_bank_base(want[k]));
        cr_tab_bank[i] = (uint8_t)want[k];
        at[k] = (int32_t)i;
        if (want[0] == want[1])
            at[k ^ 1u] = (int32_t)i;
    }
    cr_lock();
    cr_synth_tables(&cr_syn, &cr_tab[at[0]], &cr_tab[at[1]], want[1] == CR_BANK_NOISE);
    cr_unlock();
}
/* ---- presets: 0..63 the factory's (in the firmware), 64..191 the user's (in flash) ---- */
static int cr_is_user(uint32_t slot) { return slot >= CR_NFACTORY && slot < CR_NPRESET; }
static const uint8_t *cr_user_rec(uint32_t slot)   /* its 128 bytes in flash */
{
    return cr_flash_ptr(CR_FL_PRESETS + (slot - CR_NFACTORY) * CR_PATCH_BYTES);
}
static int cr_preset_saved(uint32_t slot)          /* a user slot holds a patch */
{
    const uint8_t *r;
    if (!cr_is_user(slot))
        return 1;
    r = cr_user_rec(slot);
    return r[109] == CR_PATCH_MAGIC && r[110] == CR_PATCH_VER;
}
/* preset slot n as it is now (an empty user slot: INIT) */
static void cr_preset_read(uint32_t slot, cr_patch_t *out)
{
    uint32_t i;
    slot %= CR_NPRESET;
    if (cr_is_user(slot)) {
        const uint8_t *r = cr_user_rec(slot);
        for (i = 0; i < CR_PATCH_BYTES; i++)
            ((uint8_t *)out)[i] = r[i];
        if (cr_patch_check(out) == 0)
            return;
        cr_patch_init(out, "INIT");
        return;
    }
    for (i = 0; i < CR_PATCH_BYTES; i++)
        ((uint8_t *)out)[i] = CR_FACTORY[slot][i];
    (void)cr_patch_check(out);
}
/* a sector of the user area rewritten with n bytes changed at off (read, changed, erased, written) */
static int cr_flash_change(uint32_t off, const void *src, uint32_t n)
{
    uint32_t sec = off & ~(CR_SECTOR - 1u), i;
    const uint8_t *cur = cr_flash_ptr(sec);
    for (i = 0; i < CR_SECTOR; i++)
        cr_sec[i] = cur[i];
    for (i = 0; i < n; i++)
        cr_sec[off - sec + i] = src ? ((const uint8_t *)src)[i] : 0xFFu;
    if (cr_flash_erase(sec))
        return -1;
    return cr_flash_write(sec, cr_sec, CR_SECTOR);
}
/* user slot n written (or, with p 0, emptied) */
static int cr_preset_write(uint32_t slot, const cr_patch_t *p)
{
    if (!cr_is_user(slot))
        return -1;
    return cr_flash_change(CR_FL_PRESETS + (slot - CR_NFACTORY) * CR_PATCH_BYTES, p, CR_PATCH_BYTES);
}
static int cr_ubank_write(uint32_t u, const cr_ubank_t *b)
{
    if (u >= CR_NUBANK)
        return -1;
    return cr_flash_change(CR_FL_BANKS + u * CR_SECTOR, b, sizeof *b);
}
static void cr_globals_save(void) { (void)plat_store_save(OBJ_SOUND, &cr_g, sizeof cr_g); }
static void cr_preset_label(uint32_t slot, char *b)   /* "3-1" (factory), "U12-8" (user) */
{
    uint32_t k = 0, bank;
    slot %= CR_NPRESET;
    if (cr_is_user(slot)) {
        b[k++] = 'U';
        slot -= CR_NFACTORY;
    }
    bank = slot / 8u + 1u;
    if (bank >= 10u)
        b[k++] = (char)('0' + bank / 10u);
    b[k++] = (char)('0' + bank % 10u);
    b[k++] = '-';
    b[k++] = (char)('1' + slot % 8u);
    b[k] = 0;
}

/* 0.1-0.3 kept their presets (over the factory slots) and two user banks in the store: on the first start of 0.4
 * they become user presets U1-1.. and user banks 1 and 2 (once; the store keeps them, untouched) */
#define CR_MIGRATED 0x01u
static void cr_migrate(void)
{
    static cr_patch_t t;
    uint32_t o, i, n = 0;
    int len;
    if (cr_g.pad[0] & CR_MIGRATED)
        return;
    for (o = 0; o < 2u; o++) {
        static uint8_t obj[PLAT_STORE_MAX] X0X_POOL;
        len = plat_store_load(o ? OBJ_PAT1 : OBJ_PAT0, obj, sizeof obj);
        for (i = 0; len > 0 && i < 32u && (i + 1u) * CR_PATCH_V1_BYTES <= (uint32_t)len; i++)
            if (cr_patch_from_v1(obj + i * CR_PATCH_V1_BYTES, &t) == 0 && n < CR_NUSER)
                (void)cr_preset_write(CR_NFACTORY + n++, &t);
    }
    for (o = 0; o < 2u; o++) {
        len = plat_store_load(o ? OBJ_MOTION : OBJ_SONG, &cr_ubw, sizeof cr_ubw);
        if (len == (int)sizeof cr_ubw && cr_ubw.magic == CR_UBANK_MAGIC)
            (void)cr_ubank_write(o, &cr_ubw);
    }
    cr_g.pad[0] |= CR_MIGRATED;
    cr_globals_save();
}

static void cr_lat_clear(void);
static void cr_evt(uint32_t kind)   /* tell the editor (live sync): F0 7D 46 4C 48 kind preset(lo hi) dirty F7 */
{
    uint8_t ev[10] = {0xF0, 0x7D, 0x46, 0x4C, 48, 0, 0, 0, 0, 0xF7};
    ev[5] = (uint8_t)(kind & 0x7Fu);
    ev[6] = (uint8_t)(cr_g.cur & 0x7Fu);
    ev[7] = (uint8_t)(cr_g.cur >> 7);
    ev[8] = cr_dirty;
    cr_wire(ev, sizeof ev);
}
/* a patch to the synth: every value; the banks follow from the main loop */
static void cr_use(const cr_patch_t *p)
{
    cr_lock();
    cr_p = *p;
    cr_synth_patch(&cr_syn, &cr_p);
    if (!cr_p.p[CR_ARP]) {
        cr_arp_release_all(&cr_arp);
    }
    cr_unlock();
    cr_want_tables = 1;
    cr_touch();
}
static void cr_load(uint32_t slot)
{
    static cr_patch_t t;
    cr_preset_read(slot % CR_NPRESET, &t);
    cr_g.cur = (uint8_t)(slot % CR_NPRESET);
    cr_use(&t);
    cr_dirty = 0;
    cr_evt(1);
}

/* -------------------------------------------------------------------------------------------- the notes --- */
/* out of the arp, or straight from the keys / MIDI: the synth (local) and MIDI out */
static void cr_arp_on(cr_arp_t *a, uint32_t n, uint32_t v)
{
    (void)a;
    cr_synth_on(&cr_syn, n, v);
    if (cr_g.arp_out)
        cr_midi3(0x90u | cr_g.midi_ch, n, v);
}
static void cr_arp_off(cr_arp_t *a, uint32_t n)
{
    (void)a;
    cr_synth_off(&cr_syn, n);
    if (cr_g.arp_out)
        cr_midi3(0x80u | cr_g.midi_ch, n, 0);
}
static const cr_arp_out_t cr_arp_out = {cr_arp_on, cr_arp_off};

/* a note into the instrument (inside cr_lock, or in the interrupt) */
static void cr_play(uint32_t n, uint32_t v)
{
    if (cr_p.p[CR_ARP] >= 64u)
        cr_arp_key(&cr_arp, n, v);
    else if (v)
        cr_synth_on(&cr_syn, n, v);
    else
        cr_synth_off(&cr_syn, n);
}
/* a key's note: the instrument (if LOCAL) and MIDI out (unless the arp sends its own) */
static void cr_key_play(uint32_t n, uint32_t v)
{
    if (cr_g.local) {
        cr_lock();
        cr_play(n, v);
        cr_unlock();
    }
    if (!(cr_p.p[CR_ARP] >= 64u && cr_g.arp_out))
        cr_midi3((v ? 0x90u : 0x80u) | cr_g.midi_ch, n, v);
}
static void cr_lat_clear(void)
{
    uint32_t i;
    for (i = 0; i < cr_lat_n; i++)
        cr_key_play(cr_lat_on[i], 0);
    cr_lat_n = 0;
}
static uint32_t cr_key_to_note(uint32_t k)
{
    int32_t n;
    if (cr_g.kbmode) {                           /* SCALE: the keys walk up the scale from its root */
        uint32_t m = CR_SCALE_MASK[cr_g.scale % CR_NSCALE], deg = 0, s = 0;
        n = 48 + (int32_t)cr_g.root;
        while (deg < k) {
            s++;
            if (m & (1u << (s % 12u)))
                deg++;
        }
        n += (int32_t)s;
    } else
        n = 53 + (int32_t)k;                     /* F3 .. G5, as the keys are printed */
    n += 12 * cr_oct;
    return (uint32_t)lim32(n, 0, 127);
}

/* ------------------------------------------------------------------------------------------- the values --- */
static const char *const CR_OM_SHORT[CR_NOM] = {"PM", "WSYNC", "RING", "TFOLD", "DEREZ", "R/RUN", "GLTCH", "WAMP",
    "FM", "SYNC", "MIN", "SFOLD", "CRUSH", "SCRUN", "LO-FI", "VOCAL"};
static const char *const CR_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static const char *cr_plabel(uint32_t p)
{
    switch (p) {
    case G_MSRC: return "SOURCE";
    case G_MDST: return "DEST";
    case G_MDEP: return "DEPTH";
    case G_STEPS: return "STEPS";
    case G_MCH: return "MIDI CH";
    case G_MIN: return "MIDI IN";
    case G_CLOCK: return "CLOCK";
    case G_BEND: return "BEND";
    case G_KEYS: return "KEYS";
    case G_SCALE: return "SCALE";
    case G_ROOT: return "ROOT";
    case G_VELO: return "VELO IN";
    case G_LOCAL: return "LOCAL";
    case G_BREATH: return "BREATH";
    case G_ARPOUT: return "ARP OUT";
    case G_SLOT: return "SLOT";
    case G_CPOS: return "LETTER";
    case G_CHAR: return "CHAR";
    case G_THEME: return "THEME";
    default: return p < CR_NP ? CR_P[p].name : "";
    }
}

/* parameter p's value as text: short (a knob's cell) or long (shown big) */
static const char *cr_uname(void *ctx, uint32_t u)   /* a user bank's name for cr_param_text */
{
    const cr_ubank_t *k = cr_ubank(u);
    (void)ctx;
    return k ? k->name : 0;
}
static void cr_ptext(uint32_t p, char *b, int lng)
{
    b[0] = 0;
    if (p >= G_NONE) {
        switch (p) {
        case G_MSRC: cr_cpy(b, CR_SRC_NAME[cr_msrc]); break;
        case G_MDST: cr_cpy(b, CR_DEST_NAME[cr_p.p[CR_DST0 + cr_msrc] % CR_ND]); break;
        case G_MDEP: cr_num(b, (int32_t)cr_p.p[CR_DEP0 + cr_msrc] - 64, 1); break;
        case G_STEPS: if (cr_p.steps) cr_num(b, cr_p.steps, 0); else cr_cpy(b, lng ? "NO PATTERN" : "-"); break;
        case G_MCH: cr_num(b, cr_g.midi_ch + 1, 0); break;
        case G_MIN: if (cr_g.midi_in >= 16u) cr_cpy(b, "ALL"); else cr_num(b, cr_g.midi_in + 1, 0); break;
        case G_CLOCK: cr_cpy(b, cr_g.clock == 0 ? "AUTO" : cr_g.clock == 1 ? "INT" : "MIDI"); break;
        case G_BEND: cr_num(b, cr_g.bend, 0); break;
        case G_KEYS: cr_cpy(b, cr_g.kbmode ? "SCALE" : "CHROM"); break;
        case G_SCALE: cr_cpy(b, CR_SCALE_NAME[cr_g.scale % CR_NSCALE]); if (!lng) b[5] = 0; break;
        case G_ROOT: cr_cpy(b, CR_NOTE[cr_g.root % 12u]); break;
        case G_VELO: cr_cpy(b, cr_g.velo ? "ON" : "OFF"); break;
        case G_LOCAL: cr_cpy(b, cr_g.local ? "ON" : "OFF"); break;
        case G_BREATH: cr_cpy(b, cr_g.breath == 0 ? "OFF" : cr_g.breath == 1 ? "MODW" : cr_g.breath == 2 ? "AFTT" : "EXPR"); break;
        case G_ARPOUT: cr_cpy(b, cr_g.arp_out ? "ARP" : "KEYS"); break;
        case G_SLOT: cr_preset_label(cr_save_slot, b); break;
        case G_CPOS: cr_num(b, (int32_t)cr_cpos + 1, 0); break;
        case G_THEME: cr_cpy(b, CR_THEMES[cr_g.theme % CR_NTHEME].name); if (!lng) b[5] = 0; break;
        case G_CHAR: b[0] = cr_p.name[cr_cpos] ? cr_p.name[cr_cpos] : '_'; b[1] = 0; if (b[0] == ' ') cr_cpy(b, "SPC"); break;
        default: break;
        }
        return;
    }
    cr_param_text(&cr_p, p, b, lng, cr_uname, 0);
}

/* ------------------------------------------------------------------------------------------- turning --- */
static void cr_set(uint32_t p, uint32_t v)      /* a patch value: the synth, MIDI out (its CC), the screen */
{
    v &= 0x7Fu;
    if (cr_p.p[p] == v)
        return;
    cr_lock();
    cr_p.p[p] = (uint8_t)v;
    cr_synth_param(&cr_syn, p, v);
    if (p == CR_ARP && v < 64u)
        cr_arp_release_all(&cr_arp);
    cr_unlock();
    if (p == CR_W1BANK || p == CR_W2BANK)
        cr_want_tables = 1;
    cr_midi3(0xB0u | cr_g.midi_ch, CR_P[p].cc, v);
    cr_dirty = 1;
    cr_touch();
}

/* the next value of p, d detents on, as the CRAFTsynth's knob steps it (enumerations a choice at a time) */
static uint32_t cr_step(uint32_t p, uint32_t v, int32_t d)
{
    int32_t x = (int32_t)v;
    switch (CR_P[p].kind) {
    case CR_K_BANK: {
        int w2 = p == CR_W2BANK;
        int32_t b = (int32_t)cr_bank_of(v, w2), n = w2 ? (int32_t)CR_BANKS : (int32_t)CR_BANKS - 1, i;
        i = (!w2 && b > (int32_t)CR_BANK_NOISE) ? b - 1 : b;
        i = lim32(i + d, 0, n - 1);
        b = (!w2 && i >= (int32_t)CR_BANK_NOISE) ? i + 1 : i;
        return cr_bank_val((uint32_t)b, w2);
    }
    case CR_K_OMMODE: return (uint32_t)lim32((int32_t)cr_om_of(v) + d, 0, CR_NOM - 1) * 8u + 4u;
    case CR_K_SPREAD:
        if (x >= 64 || (x == 63 && d > 0)) {      /* the chords: a shape a detent */
            int32_t sh = x >= 64 ? (int32_t)cr_spread_shape(v) : 0;
            sh = lim32(sh + d, 0, 9);
            return sh == 0 ? 63u : (uint32_t)(64 + (sh - 1) * 7 + 3);
        }
        return (uint32_t)lim32(x + d, 0, 63);
    case CR_K_SEMI: {
        int32_t s = cr_semi_of(v) + d, i;
        s = lim32(s, -48, 48);
        for (i = 0; i < 128; i++)
            if (cr_semi_of((uint32_t)i) == s)
                return (uint32_t)i;
        return v;
    }
    case CR_K_OCT: {
        int32_t o = lim32((int32_t)((v * 7u) >> 7) + d, 0, 6);
        return (uint32_t)((o * 128 + 64) / 7);
    }
    case CR_K_LMODE: {
        int32_t m = lim32((int32_t)(v < 42u ? 0 : v < 84u ? 1 : 2) + d, 0, 2);
        return m == 0 ? 20u : m == 1 ? 63u : 105u;
    }
    case CR_K_ONOFF: return d > 0 ? 127u : 0u;
    case CR_K_LSHAPE: return (uint32_t)lim32(x + d * 2, 0, 127);
    case CR_K_DEST: {
        int32_t t = x;
        do
            t = lim32(t + (d > 0 ? 1 : -1), 0, CR_ND - 1);
        while (t > 0 && t < (int32_t)CR_ND - 1 && !cr_dest_ok(p - CR_DST0, (uint32_t)t));
        if (!cr_dest_ok(p - CR_DST0, (uint32_t)t))
            return v;
        return (uint32_t)t;
    }
    case CR_K_ARPRATE: return (uint32_t)lim32((int32_t)((v * 6u) >> 7) + d, 0, 5) * 128u / 6u + 10u;
    case CR_K_BMODE: return cr_band_val((uint32_t)lim32((int32_t)cr_band(v, 5) + d, 0, 4), 5);
    case CR_K_FTYPE: return cr_band_val((uint32_t)lim32((int32_t)cr_band(v, 12) + d, 0, 11), 12);
    case CR_K_VMODE: return cr_band_val((uint32_t)lim32((int32_t)cr_band(v, 6) + d, 0, 5), 6);
    case CR_K_ERA: return cr_band_val((uint32_t)lim32((int32_t)cr_band(v, 6) + d, 0, 5), 6);
    case CR_K_LRATE1:
        if (cr_p.p[CR_L1SYNC] >= 64u)
            return (uint32_t)lim32((int32_t)((v * 16u) >> 7) + d, 0, 15) * 8u + 4u;
        break;
    case CR_K_LRATE2:
        if (x >= 64 && x + d >= 64)
            return (uint32_t)lim32((int32_t)(((v - 64u) * 8u) >> 6) + d, 0, 7) * 8u + 64u + 4u;
        if (cr_p.p[CR_L2SYNC] >= 64u && x < 64)
            return (uint32_t)lim32((int32_t)((v * 8u) >> 6) + d, 0, 8) * 8u + 4u;
        break;
    case CR_K_DTIME:
        if (cr_p.p[CR_DSYNC] >= 64u)
            return (uint32_t)lim32((int32_t)((v * 9u) >> 7) + d, 0, 8) * 128u / 9u + 7u;
        break;
    case CR_K_GLIDE:
        if ((x < 64 && x + d > 64) || (x > 64 && x + d < 64))
            return 64u;                           /* stop at OFF on the way through */
        break;
    default: break;
    }
    return (uint32_t)lim32(x + d, 0, 127);
}

static void cr_gturn(uint32_t p, int32_t d)
{
    switch (p) {
    case G_MSRC: cr_msrc = (uint32_t)lim32((int32_t)cr_msrc + d, 0, CR_NSRC - 1); break;
    case G_MDST: cr_set(CR_DST0 + cr_msrc, cr_step(CR_DST0 + cr_msrc, cr_p.p[CR_DST0 + cr_msrc], d)); break;
    case G_MDEP: cr_set(CR_DEP0 + cr_msrc, (uint32_t)lim32((int32_t)cr_p.p[CR_DEP0 + cr_msrc] + d, 1, 127)); break;
    case G_STEPS:
        cr_p.steps = (uint8_t)lim32(cr_p.steps + d, 0, cr_p.steps ? (int32_t)CR_NSTEP : 0);
        cr_dirty = 1;
        break;
    case G_MCH: cr_g.midi_ch = (uint8_t)lim32(cr_g.midi_ch + d, 0, 15); break;
    case G_MIN: cr_g.midi_in = (uint8_t)lim32(cr_g.midi_in + d, 0, 16); break;
    case G_CLOCK: cr_g.clock = (uint8_t)lim32(cr_g.clock + d, 0, 2); break;
    case G_BEND: cr_g.bend = (uint8_t)lim32(cr_g.bend + d, 0, 24); break;
    case G_KEYS: cr_g.kbmode = d > 0; break;
    case G_SCALE: cr_g.scale = (uint8_t)lim32(cr_g.scale + d, 0, CR_NSCALE - 1); break;
    case G_ROOT: cr_g.root = (uint8_t)(((int32_t)cr_g.root + d) % 12 + 12) % 12; break;
    case G_VELO: cr_g.velo = d > 0; break;
    case G_LOCAL: cr_g.local = d > 0; break;
    case G_BREATH: cr_g.breath = (uint8_t)lim32(cr_g.breath + d, 0, 3); break;
    case G_ARPOUT: cr_g.arp_out = d > 0; break;
    case G_THEME: cr_g.theme = (uint8_t)lim32(cr_g.theme + d, 0, CR_NTHEME - 1); break;
    case G_SLOT: cr_save_slot = (uint32_t)lim32((int32_t)cr_save_slot + d, CR_NFACTORY, CR_NPRESET - 1); break;
    case G_CPOS: cr_cpos = (uint32_t)lim32((int32_t)cr_cpos + d, 0, 10); break;
    case G_CHAR: {
        static const char cs[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+/#&";
        int32_t i = 0, n = (int32_t)sizeof cs - 1;
        char c = cr_p.name[cr_cpos];
        while (i < n && cs[i] != c)
            i++;
        if (i >= n)
            i = 0;
        i = ((i + d) % n + n) % n;
        cr_p.name[cr_cpos] = cs[i];
        for (i = 0; i < (int32_t)cr_cpos; i++)   /* no holes before it */
            if (!cr_p.name[i])
                cr_p.name[i] = ' ';
        cr_dirty = 1;
        break;
    }
    default: break;
    }
    cr_touch();
}

static void cr_turn(uint32_t p, int32_t d)
{
    if (p >= G_NONE) {
        cr_gturn(p, d);
        return;
    }
    cr_set(p, cr_step(p, cr_p.p[p], d));
}

/* LFO / MOD EG assign: the source held, a knob turned: that knob's value is its destination, the turn its depth */
static int cr_dest_of_param(uint32_t p)
{
    uint32_t d;
    static const uint8_t DP[CR_ND] = {0xFF, CR_W1POS, CR_W2POS, CR_MIX, 0xFF, CR_TUNE, CR_OMAMT, CR_L1RATE,
        CR_L2RATE, CR_L1SHAPE, CR_L2SHAPE, CR_CUT, CR_RES, CR_MORPH, CR_FEGAMT, CR_FA, CR_FD, CR_FS, CR_FR,
        CR_AEGAMT, CR_AA, CR_AD, CR_AS, CR_AR, CR_VOL, CR_MA, CR_MD, CR_MS, CR_MR, CR_DIST, CR_DMIX, CR_DTIME,
        CR_DFB, CR_SPREAD, CR_GLIDE, 0xFF, 0xFF, 0xFF, CR_BAMT, CR_VMIX, CR_CHMIX, CR_PHMIX, CR_CRUSH, CR_OTT};
    if (p == CR_OCT)
        return CR_D_PITCH;
    if (p == CR_FINE)
        return CR_D_O2PITCH;
    for (d = 1; d < CR_ND; d++)
        if (DP[d] == p)
            return (int)d;
    return -1;
}
static void cr_assign(uint32_t src, uint32_t p, int32_t d)
{
    int dst = p < CR_NP ? cr_dest_of_param(p) : -1;
    char b[28];
    if (dst < 0 || !cr_dest_ok(src, (uint32_t)dst)) {
        cr_flash("CAN'T MODULATE THAT");
        return;
    }
    if (cr_p.p[CR_DST0 + src] != (uint32_t)dst) {
        cr_set(CR_DST0 + src, (uint32_t)dst);
        cr_set(CR_DEP0 + src, 64);
    }
    cr_set(CR_DEP0 + src, (uint32_t)lim32((int32_t)cr_p.p[CR_DEP0 + src] + d, 1, 127));
    cr_cpy(b, CR_SRC_NAME[src]);
    cr_cat(b, " TO ");
    cr_cat(b, CR_DEST_NAME[dst]);
    cr_cat(b, " ");
    cr_num(b + cr_len(b), (int32_t)cr_p.p[CR_DEP0 + src] - 64, 1);
    cr_flash(b);
}

/* --------------------------------------------------------------------------------------------- the panel --- */
static void cr_go(uint32_t pg)
{
    cr_page = pg;
    cr_hit = -1;
    cr_touch();
}
static void cr_cycle(uint32_t a, uint32_t b, uint32_t c)
{
    cr_go(cr_page == a ? b : cr_page == b && c != b ? c : a);
}

static void cr_button(uint32_t b)                /* a press (LFO, SEL, ENV: a release without a knob turned) */
{
    switch (b) {
    case B_HOME:                                 /* OSC -> WAVES -> OSC B -> TUNE */
        cr_go(cr_page < PG_TUNE ? cr_page + 1u : PG_OSC);
        break;
    case B_EDIT: cr_go(PG_FILTER); break;
    case B_ENV: cr_cycle(PG_FEG, PG_AEG, PG_MEG); break;
    case B_LFO: cr_cycle(PG_LFO1, PG_LFO2, PG_LFO2); break;
    case B_SEL: cr_go(PG_MATRIX); break;
    case B_FX:                                   /* FX -> OUTPUT -> REVERB -> REVERB 2 -> MOD FX -> CRUSH */
        cr_go(cr_page >= PG_FX && cr_page < PG_CRUSH ? cr_page + 1u : PG_FX);
        break;
    case B_GLO: cr_cycle(PG_GLOB, PG_GLOB2, PG_GLOB3); break;
    case B_ARP:
        if (cr_page != PG_ARP)
            cr_go(PG_ARP);
        cr_set(CR_ARP, cr_p.p[CR_ARP] >= 64u ? 0u : 127u);
        cr_flash(cr_p.p[CR_ARP] >= 64u ? "ARP ON" : "ARP OFF");
        break;
    case B_SEQ: cr_go(PG_SEQ); break;
    case B_REC:
        cr_latch ^= 1u;
        if (!cr_latch)
            cr_lat_clear();
        cr_flash(cr_latch ? "LATCH ON" : "LATCH OFF");
        break;
    case B_PLAY:
        if (cr_page == PG_SAVE) {
            static cr_patch_t t;
            cr_patch_init(&t, "INIT");
            cr_use(&t);
            cr_dirty = 1;
            cr_flash("INIT PATCH");
            break;
        }
        cr_app_panic();
        cr_flash("ALL NOTES OFF");
        break;
    case B_SAVE:
        if (cr_page != PG_SAVE) {
            cr_save_slot = cr_g.cur;             /* a user preset saves over itself; a factory one into a free slot */
            if (!cr_is_user(cr_save_slot)) {
                for (cr_save_slot = CR_NFACTORY; cr_save_slot < CR_NPRESET - 1u && cr_preset_saved(cr_save_slot); cr_save_slot++)
                    ;
            }
            cr_cpos = 0;
            cr_go(PG_SAVE);
        } else {
            cr_app_panic();
            if (cr_preset_write(cr_save_slot, &cr_p) == 0) {
                cr_g.cur = (uint8_t)cr_save_slot;
                cr_dirty = 0;
                cr_globals_save();
                cr_evt(2);
                cr_go(PG_OSC);
                cr_flash("SAVED");
            } else
                cr_flash("SAVE FAILED");
        }
        break;
    case B_OCTDN:
    case B_OCTUP:
        cr_oct = lim32(cr_oct + (b == B_OCTUP ? 1 : -1), -3, 3);
        cr_touch();
        break;
    default: break;
    }
}

void cr_app_input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), k, ch, i;
    int32_t d;
    if (cr_want_preset >= 0) {                   /* a program change came in */
        uint32_t pc = (uint32_t)cr_want_preset;
        cr_want_preset = -1;
        cr_load(pc);
    }
    if (cr_want_tables) {
        cr_want_tables = 0;
        cr_tables();
        cr_touch();
    }
    if (cr_want_draw) {
        cr_want_draw = 0;
        cr_touch();
    }
    /* keys */
    ch = keys ^ cr_keys_prev;
    for (k = 0; ch && k < 27u; k++)
        if (ch & (1u << k)) {
            int down = (keys >> k) & 1u;
            if (down) {
                uint32_t n = cr_key_to_note(k);
                if (btn & (1u << B_SEQ)) {       /* SEQ held: a step of the pattern */
                    cr_btn_used |= 1u << B_SEQ;
                    if (!cr_p.steps) {
                        cr_p.pad = (uint8_t)n;   /* the pattern's root */
                        cr_p.step[0] = 64;
                        cr_p.steps = 1;
                    } else if (cr_p.steps < CR_NSTEP)
                        cr_p.step[cr_p.steps++] = (uint8_t)lim32(64 + (int32_t)n - (int32_t)cr_p.pad, 1, 127);
                    cr_dirty = 1;
                    cr_touch();
                }
                if (cr_latch && !(cr_keys_prev & 0x7FFFFFFu))
                    cr_lat_clear();              /* a new chord replaces the latched one */
                cr_key_note[k] = (uint8_t)n;
                cr_key_play(n, 100);
                if (cr_latch && cr_lat_n < sizeof cr_lat_on)
                    cr_lat_on[cr_lat_n++] = (uint8_t)n;
                cr_keys_down++;
            } else if (!cr_latch)
                cr_key_play(cr_key_note[k], 0);
        }
    cr_keys_prev = keys;
    /* buttons: most act on press; LFO, SEL and ENV on release, unless a knob was turned while they were held */
    ch = btn ^ cr_btn_prev;
    if ((btn & ((1u << B_OCTDN) | (1u << B_OCTUP))) != ((1u << B_OCTDN) | (1u << B_OCTUP)))
        for (k = 0; k < NB; k++) {
            uint32_t bit = 1u << k;
            int hold = k == B_LFO || k == B_SEL || k == B_ENV || k == B_SEQ;
            if (!(ch & bit))
                continue;
            if (btn & bit) {
                cr_btn_used &= ~bit;
                if (k == B_REC && (btn & (1u << B_SEQ))) {      /* SEQ + REC: a rest */
                    cr_btn_used |= 1u << B_SEQ;
                    if (cr_p.steps && cr_p.steps < CR_NSTEP)
                        cr_p.step[cr_p.steps++] = CR_REST;
                    cr_dirty = 1;
                    cr_touch();
                    continue;
                }
                if (k == B_PLAY && (btn & (1u << B_SEQ))) {     /* SEQ + PLAY: the pattern cleared */
                    cr_btn_used |= 1u << B_SEQ;
                    cr_p.steps = 0;
                    cr_dirty = 1;
                    cr_flash("PATTERN CLEARED");
                    continue;
                }
                if (cr_page == PG_SAVE && k != B_SAVE && k != B_PLAY) {
                    cr_go(PG_OSC);
                    cr_flash("NOT SAVED");
                    continue;
                }
                if (!hold)
                    cr_button(k);
            } else if (hold && !(cr_btn_used & bit))
                cr_button(k);
        }
    cr_btn_prev = btn;
    /* encoders */
    if ((d = plat_enc(EN_PRESET)) != 0) {
        if (cr_page == PG_SAVE)
            cr_gturn(G_SLOT, d);
        else {
            cr_load((uint32_t)(((int32_t)cr_g.cur + d) % (int32_t)CR_NPRESET + (int32_t)CR_NPRESET) % CR_NPRESET);
            cr_hit = -1;
        }
    }
    if ((d = plat_enc(EN_SELECT)) != 0)
        cr_go((uint32_t)lim32((int32_t)cr_page + d, 0, PG_SAVE - 1));
    if ((d = plat_enc(EN_ALGO)) != 0) {
        cr_turn(CR_OMMODE, d);
        cr_hit = 4;                              /* shown big, as a knob's */
        cr_hit_p = CR_OMMODE;
        cr_hit_until = now + 1500u;
    }
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) {
            uint32_t p = CR_PAGE_P[cr_page][k];
            if (p == G_NONE)
                continue;
            if (btn & (1u << B_LFO)) {           /* hold LFO + a knob: assign the LFO on screen (LFO 1 elsewhere) */
                cr_btn_used |= 1u << B_LFO;
                cr_assign(cr_page == PG_LFO2 ? CR_S_LFO2 : CR_S_LFO1, p, d);
                continue;
            }
            if (btn & (1u << B_SEL)) {           /* hold SEL + a knob: assign the MOD EG */
                cr_btn_used |= 1u << B_SEL;
                cr_assign(CR_S_MEG, p, d);
                continue;
            }
            if ((btn & (1u << B_ENV)) && cr_page >= PG_FEG && cr_page <= PG_MEG) {   /* hold ENV: all three EGs */
                uint32_t e, base = p - CR_PAGE_P[cr_page][0];
                cr_btn_used |= 1u << B_ENV;
                for (e = 0; e < 3u; e++)
                    cr_turn(CR_PAGE_P[PG_FEG + e][0] + base, d);
                cr_flash("ALL ENVELOPES");
            } else
                cr_turn(p, d);
            cr_hit = (int32_t)k;
            cr_hit_p = p;
            cr_hit_until = now + 1500u;
        }
    if (cr_hit >= 0 && (int32_t)(now - cr_hit_until) >= 0) {
        cr_hit = -1;
        cr_touch();
    }
    if (cr_msg[0] && (int32_t)(now - cr_msg_until) >= 0) {
        cr_msg[0] = 0;
        cr_touch();
    }
    {
        static uint32_t cpu_t;
        if (cr_page == PG_GLOB3 && now - cpu_t > 500u) {   /* the load, twice a second */
            cpu_t = now;
            cr_touch();
        }
    }
    if (cr_clock_seen && now - cr_clock_ms > 500u)
        cr_clock_seen = 0;
    {   /* the lights */
        uint32_t lit = 0, kl = 0;
        for (i = 0; i < 27u; i++)
            if (keys & (1u << i))
                kl |= 1u << i;
        if (cr_p.p[CR_ARP] >= 64u) lit |= 1u << B_ARP;
        if (cr_latch) lit |= 1u << B_REC;
        if (cr_oct < 0) lit |= 1u << B_OCTDN;
        if (cr_oct > 0) lit |= 1u << B_OCTUP;
        if (cr_page <= PG_TUNE) lit |= 1u << B_HOME;
        if (cr_page == PG_FILTER) lit |= 1u << B_EDIT;
        if (cr_page >= PG_FEG && cr_page <= PG_MEG) lit |= 1u << B_ENV;
        if (cr_page == PG_LFO1 || cr_page == PG_LFO2) lit |= 1u << B_LFO;
        if (cr_page == PG_MATRIX) lit |= 1u << B_SEL;
        if (cr_page >= PG_FX && cr_page <= PG_CRUSH) lit |= 1u << B_FX;
        if (cr_page == PG_SEQ || cr_p.steps) lit |= 1u << B_SEQ;
        if (cr_page >= PG_GLOB && cr_page <= PG_GLOB3) lit |= 1u << B_GLO;
        if (cr_page == PG_SAVE) lit |= 1u << B_SAVE;
        if (cr_syn.gate) lit |= 1u << B_PLAY;
        plat_keys_dim(0);
        plat_leds(lit, kl);
    }
    {   /* the globals: saved a moment after they settle */
        static cr_globals_t saved;
        static uint32_t t;
        uint32_t same = 1;
        const uint8_t *a = (const uint8_t *)&cr_g, *bb = (const uint8_t *)&saved;
        for (i = 0; i < sizeof cr_g; i++)
            if (i != 8u && a[i] != bb[i])        /* (cur: saved with SAVE, not as you turn PRESET) */
                same = 0;
        if (same)
            t = now;
        else if (now - t > 2000u) {
            saved = cr_g;
            cr_globals_save();
        }
    }
}

/* ------------------------------------------------------------------------------------------------ screen --- */
/* The look: after Serum 2 and Shrub (a top bar, two oscillator panels with their wavetables stacked in 3D, arc
 * knobs), in one of the themes below (GLO -> SETUP -> THEME). Type stays big: values 23 px, the one turned 44 px. */
static const cr_theme_t *cr_th(void) { return &CR_THEMES[cr_g.theme % CR_NTHEME]; }
static int32_t cr_ty;                            /* the band being drawn starts at this screen row */

static uint16_t cr_mix(uint16_t a, uint16_t b, uint32_t t)   /* t 0..256: a .. b */
{
    uint32_t r = ((a >> 11) * (256u - t) + (b >> 11) * t) >> 8, g = (((a >> 5) & 63u) * (256u - t) + ((b >> 5) & 63u) * t) >> 8;
    uint32_t bl = ((a & 31u) * (256u - t) + (b & 31u) * t) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
/* drawing at screen rows (the band clips) */
static void sx_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(x, y - cr_ty, w, h, c); }
static void sx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) { cv_line(x0, y0 - cr_ty, x1, y1 - cr_ty, c); }
static void sx_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { (void)cv_text(x, y - cr_ty, f, s, c); }
static void sx_center(int32_t x, int32_t w, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    sx_text(x + (w - text_w(f, s)) / 2, y, f, s, c);
}
static void sx_thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    sx_line(x0, y0, x1, y1, c);
    sx_line(x0, y0 + 1, x1, y1 + 1, c);
}
static void sx_round(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a panel with cut corners */
{
    sx_rect(x + 2, y, w - 4, h, c);
    sx_rect(x, y + 2, w, h - 4, c);
    sx_rect(x + 1, y + 1, w - 2, h - 2, c);
}
static const felucca_font_t *cr_fit(const char *s, int32_t w)
{
    if (text_w(&FONT_M, s) <= w)
        return &FONT_M;
    if (text_w(&FONT_B, s) <= w)
        return &FONT_B;
    return &FONT_S;
}

/* a wave of a bank at a morph position 0..1: sample k (0..255), -1..1 */
static float cr_wave_at(const int16_t (*b)[CR_WT_N], float pos, uint32_t k, uint32_t bank)
{
    float m = pos * 4.f, f, v;
    uint32_t wa = (uint32_t)m;
    if (wa > 3u)
        wa = 3u;
    f = m - (float)wa;
    v = ((float)b[wa][k] + ((float)b[wa + 1u][k] - (float)b[wa][k]) * f) * (1.f / 26000.f);
    if (bank == CR_BANK_NOISE && pos > 0.f) {                /* the noise bank: a sine going to noise */
        float n = (float)(((k * 2654435761u) >> 23) & 255u) * (1.f / 128.f) - 1.f, nm = pos * 2.f > 1.f ? 1.f : pos * 2.f;
        v += (n - v) * nm;
    }
    return v;
}

/* a wavetable as Serum draws it: its frames stacked in depth, the one playing bright and filled */
#define CR_FRAMES 11
static void cr_wt3d(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bank, uint32_t pos127, uint16_t acc)
{
    const cr_theme_t *t = cr_th();
    const int16_t (*b)[CR_WT_N] = cr_bank_base(bank);
    const int32_t dx = 3, dy = 4, fw = w - dx * (CR_FRAMES - 1), amp = (h - dy * (CR_FRAMES - 1)) * 9 / 20;
    float pos = (float)pos127 / 127.f;
    int32_t f, i, sel_ox, sel_oy, yb, py = 0;
    for (f = CR_FRAMES - 1; f >= 0; f--) {                   /* back to front, faint */
        int32_t ox = x + f * dx, oy = y + h - amp - 2 - f * dy;
        uint16_t c = cr_mix(t->panel, acc, 60u + (uint32_t)(CR_FRAMES - f) * 9u);   /* Serum's tinted stack, fading back */
        for (i = 0; i <= fw; i += 2) {
            int32_t yy = oy - (int32_t)(cr_wave_at(b, (float)f / (float)(CR_FRAMES - 1), (uint32_t)(i * 255 / fw), bank) * (float)amp);
            if (i)
                sx_line(ox + i - 2, py, ox + i, yy, c);
            py = yy;
        }
    }
    sel_ox = x + (int32_t)(pos * (float)(dx * (CR_FRAMES - 1)) + 0.5f);   /* the frame playing, where it sits */
    sel_oy = y + h - amp - 2 - (int32_t)(pos * (float)(dy * (CR_FRAMES - 1)) + 0.5f);
    yb = sel_oy;
    for (i = 0; i <= fw; i++) {
        int32_t yy = sel_oy - (int32_t)(cr_wave_at(b, pos, (uint32_t)(i * 255 / fw), bank) * (float)amp);
        sx_line(sel_ox + i, yb, sel_ox + i, yy, cr_mix(t->panel, acc, 90u));   /* filled to its middle */
        if (i)
            sx_thick(sel_ox + i - 1, py, sel_ox + i, yy, acc);
        py = yy;
    }
    sx_line(sel_ox, yb, sel_ox + fw, yb, cr_mix(t->panel, acc, 140u));
}

/* an arc knob (Serum's): a 270-degree track, the value's arc over it, bipolar ones from the top */
static void cr_knob(int32_t cx, int32_t cy, int32_t r, float v, int bip, uint16_t acc)
{
    const cr_theme_t *t = cr_th();
    int32_t i;
    float from = bip ? 0.5f : 0.f;
    for (i = 0; i <= 96; i++) {
        float u = (float)i / 96.f, ang = 0.375f + u * 0.75f;  /* turns, from bottom-left round the top to bottom-right */
        float sn = cr_sin_turn(ang - (float)(int32_t)ang), cs = cr_sin_turn(cr_frac(ang + 0.25f));
        int on = (u >= (from < v ? from : v) && u <= (from < v ? v : from));
        int32_t k;
        for (k = -1; k <= 2; k++) {
            int32_t px = cx + (int32_t)(cs * (float)(r + k)), py = cy + (int32_t)(sn * (float)(r + k));
            sx_rect(px, py, 1, 1, on ? acc : t->track);
        }
    }
    {
        float ang = 0.375f + v * 0.75f, sn = cr_sin_turn(cr_frac(ang)), cs = cr_sin_turn(cr_frac(ang + 0.25f));
        sx_thick(cx, cy, cx + (int32_t)(cs * (float)(r - 4)), cy + (int32_t)(sn * (float)(r - 4)), t->ink);
    }
}

static void cr_draw_env(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t a)
{
    const cr_theme_t *t = cr_th();
    float tt[4], tot, sxs;
    uint32_t i;
    int32_t px[5], py[5], k;
    for (i = 0; i < 4u; i++) {
        float v = (float)cr_p.p[a + i] / 127.f;
        tt[i] = i == 2u ? 0.6f : 0.05f + 4.f * v * v * v;
    }
    tot = tt[0] + tt[1] + tt[2] + tt[3];
    sxs = (float)w / tot;
    px[0] = x;
    px[1] = px[0] + (int32_t)(tt[0] * sxs);
    px[2] = px[1] + (int32_t)(tt[1] * sxs);
    px[3] = px[2] + (int32_t)(tt[2] * sxs);
    px[4] = x + w;
    py[0] = py[4] = y + h;
    py[1] = y;
    py[2] = py[3] = y + h - (int32_t)((float)cr_p.p[a + 2u] / 127.f * (float)h);
    for (k = 0; k < 4; k++) {                    /* filled under, as Serum's envelopes */
        int32_t xx;
        for (xx = px[k]; xx <= px[k + 1]; xx++) {
            int32_t yy = px[k + 1] == px[k] ? py[k] : py[k] + (py[k + 1] - py[k]) * (xx - px[k]) / (px[k + 1] - px[k]);
            sx_line(xx, yy, xx, y + h, cr_mix(t->panel, t->acc, 70u));
        }
    }
    for (k = 0; k < 4; k++)
        sx_thick(px[k], py[k], px[k + 1], py[k + 1], t->acc);
    for (k = 1; k < 4; k++)
        sx_rect(px[k] - 2, py[k] - 2, 5, 5, t->ink);
}

static void cr_draw_lfo(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t sh)
{
    const cr_theme_t *t = cr_th();
    float s = (float)cr_p.p[sh] / 127.f * 4.f, f;
    uint32_t k = (uint32_t)s, i, r = 0x1234u;
    int32_t py = 0, mid = y + h / 2;
    float hold = 0.f;
    if (k > 3u)
        k = 3u;
    f = s - (float)k;
    sx_line(x, mid, x + w, mid, t->line);
    for (i = 0; i <= (uint32_t)w; i++) {
        float ph = (float)i / (float)w * 2.f, v[5], val;
        int32_t yy;
        ph -= (float)(int32_t)ph;
        if ((i * 8u) % (uint32_t)w < 8u) {
            r = r * 1103515245u + 12345u;
            hold = (float)((r >> 16) & 255u) / 128.f - 1.f;
        }
        v[0] = cr_sin_turn(ph);
        v[1] = ph < 0.25f ? 4.f * ph : ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f;
        v[2] = 2.f * ph - 1.f;
        v[3] = ph < 0.5f ? 1.f : -1.f;
        v[4] = hold;
        val = v[k] + (v[k + 1u] - v[k]) * f;
        yy = mid - (int32_t)(val * (float)h * 0.45f);
        sx_line(x + (int32_t)i, mid, x + (int32_t)i, yy, cr_mix(t->panel, t->acc, 70u));
        if (i)
            sx_thick(x + (int32_t)i - 1, py, x + (int32_t)i, yy, t->acc);
        py = yy;
    }
}

static void cr_draw_filter(int32_t x, int32_t y, int32_t w, int32_t h)
{
    const cr_theme_t *t = cr_th();
    float fc = (float)cr_p.p[CR_CUT] / 127.f * 10.f, k = 2.f * (1.f - 0.985f * (float)cr_p.p[CR_RES] / 127.f);
    float m = (float)cr_p.p[CR_MORPH] / 127.f, wl = m < 0.5f ? 1.f - 2.f * m : 0.f, wh = m < 0.5f ? 0.f : 2.f * m - 1.f;
    float wb = m < 0.5f ? 2.f * m : 2.f - 2.f * m;
    uint32_t i;
    int32_t py = 0, g;
    for (g = 1; g < 10; g++)                     /* octave lines */
        sx_line(x + g * w / 10, y, x + g * w / 10, y + h, t->line);
    for (i = 0; i <= (uint32_t)w; i++) {
        float oc = (float)i / (float)w * 10.f, r = cr_exp2f(oc - fc), r2 = r * r;
        float re = 1.f - r2, im = k * r, den = re * re + im * im, nr = wl - wh * r2, ni = wb * r, hr, hi, db;
        int32_t yy;
        hr = (nr * re + ni * im) / den;
        hi = (ni * re - nr * im) / den;
        db = 10.f * cr_log2f(hr * hr + hi * hi + 1e-9f) * 0.30103f;
        db = db < -36.f ? -36.f : db > 18.f ? 18.f : db;
        yy = y + (int32_t)((18.f - db) / 54.f * (float)h);
        sx_line(x + (int32_t)i, yy, x + (int32_t)i, y + h, cr_mix(t->panel, t->acc, 70u));
        if (i)
            sx_thick(x + (int32_t)i - 1, py, x + (int32_t)i, yy, t->acc);
        py = yy;
    }
}

/* where a parameter's value sits on its knob (0..1), and whether it is bipolar */
static float cr_knob_pos(uint32_t p, int *bip)
{
    *bip = 0;
    if (p >= G_NONE) {
        if (p == G_MDEP) {
            *bip = 1;
            return (float)cr_p.p[CR_DEP0 + cr_msrc] / 127.f;
        }
        if (p == G_MDST)
            return (float)cr_p.p[CR_DST0 + cr_msrc] / (float)(CR_ND - 1u);
        if (p == G_MSRC)
            return (float)cr_msrc / 7.f;
        if (p == G_STEPS)
            return (float)cr_p.steps / 16.f;
        if (p == G_THEME)
            return (float)(cr_g.theme % CR_NTHEME) / (float)(CR_NTHEME - 1u);
        return -1.f;                             /* no knob: the value alone */
    }
    if (CR_P[p].kind == CR_K_DEST)
        return (float)cr_p.p[p] / (float)(CR_ND - 1u);
    *bip = CR_P[p].kind == CR_K_BIP || CR_P[p].kind == CR_K_SEMI || CR_P[p].kind == CR_K_CENT || CR_P[p].kind == CR_K_GLIDE;
    return (float)cr_p.p[p] / 127.f;
}

static void cr_screen(void)
{
    const cr_theme_t *t = cr_th();
    char b[40], s2[16];
    uint32_t i;
    /* ---- the top bar: the preset in a box, the page ---- */
    sx_rect(0, 0, 240, 32, t->bar);
    sx_round(3, 3, 150, 26, t->panel);
    cr_preset_label(cr_g.cur, b);
    sx_text(8, 5, &FONT_M, b, t->acc);
    for (i = 0; i < 11u && cr_p.name[i]; i++)
        s2[i] = cr_p.name[i];
    s2[i] = 0;
    if (cr_dirty)
        cr_cat(s2, "*");
    {
        int32_t x = 12 + text_w(&FONT_M, b);
        sx_text(x, 5, cr_fit(s2, 150 - x), s2, t->ink);
    }
    sx_center(156, 84, 9, text_w(&FONT_B, CR_PG_NAME[cr_page]) <= 82 ? &FONT_B : &FONT_S, CR_PG_NAME[cr_page], t->ink);
    /* ---- the middle (34..148) ---- */
    if (cr_msg[0]) {
        sx_round(4, 36, 232, 110, t->hil);
        sx_center(4, 232, 76, cr_fit(cr_msg, 228), cr_msg, t->ink);
    } else if (cr_hit >= 0) {
        sx_round(4, 36, 232, 110, t->panel);
        sx_center(4, 232, 46, &FONT_M, cr_plabel(cr_hit_p), t->soft);
        cr_ptext(cr_hit_p, b, 1);
        sx_center(4, 232, 80, text_w(&FONT_XL, b) <= 228 ? &FONT_XL : &FONT_M, b, t->ink);
    } else if (cr_page <= PG_TUNE) {             /* OSC A and OSC B, Serum's way */
        uint32_t k;
        for (k = 0; k < 2u; k++) {
            int32_t x = 4 + (int32_t)k * 118;
            uint32_t bank = cr_bank_of(cr_p.p[k ? CR_W2BANK : CR_W1BANK], (int)k);
            uint16_t acc = k ? t->acc2 : t->acc;
            sx_round(x, 36, 114, 110, t->panel);
            sx_rect(x + 4, 40, 18, 18, acc);
            sx_center(x + 4, 18, 41, &FONT_B, k ? "B" : "A", t->panel);
            cr_ptext(k ? CR_W2BANK : CR_W1BANK, b, 1);
            b[10] = 0;
            sx_text(x + 26, 41, &FONT_B, b, t->ink);
            cr_wt3d(x + 5, 62, 104, 66, bank, cr_p.p[k ? CR_W2POS : CR_W1POS], acc);
            cr_cpy(b, "POS ");
            cr_num(b + 4, cr_p.p[k ? CR_W2POS : CR_W1POS], 0);
            sx_text(x + 6, 129, &FONT_S, b, t->soft);
            if (!k) {
                cr_cpy(b, CR_OM_SHORT[cr_om_of(cr_p.p[CR_OMMODE])]);
                sx_text(x + 108 - text_w(&FONT_S, b), 129, &FONT_S, b, acc);
            }
        }
    } else {
        sx_round(4, 36, 232, 110, t->panel);
        if (cr_page >= PG_FEG && cr_page <= PG_MEG)
            cr_draw_env(14, 48, 212, 86, CR_PAGE_P[cr_page][0]);
        else if (cr_page == PG_LFO1 || cr_page == PG_LFO2) {
            uint32_t sl = cr_page == PG_LFO1 ? 0u : 1u, dst = cr_p.p[CR_DST0 + sl];
            cr_draw_lfo(12, 46, 140, 90, cr_page == PG_LFO1 ? CR_L1SHAPE : CR_L2SHAPE);
            sx_text(160, 50, &FONT_S, "TO", t->soft);
            sx_text(160, 68, cr_fit(CR_DEST_NAME[dst], 72), CR_DEST_NAME[dst], t->ink);
            if (dst) {
                cr_num(b, (int32_t)cr_p.p[CR_DEP0 + sl] - 64, 1);
                sx_text(160, 96, &FONT_M, b, t->acc);
            }
        } else if (cr_page == PG_FILTER) {
            const char *fn = CR_FTYPE_NAME[cr_band(cr_p.p[CR_FTYPE], 12u)];
            cr_draw_filter(12, 44, 216, 94);
            sx_rect(14, 46, text_w(&FONT_M, fn) + 8, 22, t->panel);
            sx_text(18, 48, &FONT_M, fn, t->ink);
        }
        else if (cr_page == PG_MATRIX) {
            sx_center(4, 232, 48, &FONT_M, CR_SRC_NAME[cr_msrc], t->acc);
            sx_center(4, 232, 76, &FONT_S, "TO", t->soft);
            sx_center(4, 232, 96, cr_fit(CR_DEST_NAME[cr_p.p[CR_DST0 + cr_msrc] % CR_ND], 228),
                      CR_DEST_NAME[cr_p.p[CR_DST0 + cr_msrc] % CR_ND], t->ink);
        } else if (cr_page == PG_SEQ || (cr_page == PG_ARP && cr_p.steps)) {
            for (i = 0; i < CR_NSTEP; i++) {
                int32_t x = 10 + (int32_t)(i % 8u) * 28, y = 46 + (int32_t)(i / 8u) * 46;
                if (i < cr_p.steps && cr_p.step[i] != CR_REST) {
                    sx_round(x, y, 26, 40, t->acc);
                    cr_num(s2, (int32_t)cr_p.step[i] - 64, 0);
                    sx_center(x, 26, y + 12, &FONT_B, s2, t->panel);
                } else
                    sx_round(x, y, 26, 40, i < cr_p.steps ? t->line : t->track);
            }
        } else if (cr_page == PG_GLOB3) {        /* the audio's load, to see how far the effects go */
            cr_cpy(b, "CPU ");
            cr_num(b + 4, (int32_t)plat_cpu_pct(), 0);
            cr_cat(b, "%");
            sx_center(4, 232, 46, &FONT_M, "AUDIO LOAD", t->soft);
            sx_center(4, 232, 80, &FONT_XL, b, t->ink);
        } else if (cr_page == PG_SAVE) {
            sx_center(4, 232, 44, &FONT_S, cr_preset_saved(cr_save_slot) ? "OVERWRITES THIS PRESET" : "INTO AN EMPTY SLOT", t->soft);
            for (i = 0; i < 11u; i++) {
                int32_t x = 10 + (int32_t)i * 20;
                char c[2] = {cr_p.name[i] ? cr_p.name[i] : ' ', 0};
                if (i == cr_cpos)
                    sx_rect(x, 70, 19, 32, t->hil);
                sx_rect(x, 102, 19, 2, t->ink);
                sx_center(x, 19, 74, &FONT_M, c, t->ink);
            }
            sx_center(4, 232, 116, &FONT_S, "SAVE: SAVE    PLAY: INIT", t->soft);
        } else {
            cr_ptext(CR_PAGE_P[cr_page][0], b, 1);
            sx_center(4, 232, 46, &FONT_M, cr_plabel(CR_PAGE_P[cr_page][0]), t->soft);
            sx_center(4, 232, 80, text_w(&FONT_XL, b) <= 228 ? &FONT_XL : &FONT_M, b, t->ink);
        }
    }
    /* ---- the knobs (150..240) ---- */
    sx_rect(0, 150, 240, 90, t->bar);
    for (i = 0; i < 4u; i++) {
        uint32_t p = CR_PAGE_P[cr_page][i];
        int32_t x = (int32_t)i * 60;
        const char *l;
        float v;
        int bip;
        if (p == G_NONE)
            continue;
        sx_round(x + 2, 152, 56, 86, (int32_t)i == cr_hit ? t->hil : t->panel);
        l = cr_plabel(p);
        sx_center(x + 2, 56, 154, text_w(&FONT_S, l) <= 54 ? &FONT_S : &FONT_XS, l, t->soft);
        v = cr_knob_pos(p, &bip);
        if (v >= 0.f)
            cr_knob(x + 30, 188, 14, v, bip, i & 1u ? t->acc2 : t->acc);
        cr_ptext(p, b, 0);
        sx_center(x + 2, 56, v >= 0.f ? 208 : 186, cr_fit(b, 54), b, t->ink);
    }
    /* what is on: ARP / SEQ, LATCH, the octave (a strip under the bar) */
    b[0] = 0;
    if (cr_p.p[CR_ARP] >= 64u)
        cr_cat(b, cr_p.steps ? "SEQ " : "ARP ");
    if (cr_latch)
        cr_cat(b, "LATCH ");
    if (cr_oct) {
        cr_cat(b, "OCT");
        cr_num(b + cr_len(b), cr_oct, 1);
    }
    if (b[0] && !cr_msg[0] && cr_hit < 0 && cr_page > PG_TUNE)
        sx_text(232 - text_w(&FONT_S, b), 129, &FONT_S, b, t->acc);
}

void cr_app_draw(int force)
{
    if (!force && cr_drawn == cr_ver)
        return;
    cr_drawn = cr_ver;
    cv_begin(240, 120, cr_th()->bg);
    cr_ty = 0;
    cr_screen();
    cv_blit(0, 0);
    cv_begin(240, 120, cr_th()->bg);
    cr_ty = 120;
    cr_screen();
    cv_blit(0, 120);
}

/* --------------------------------------------------------------------------------------------- the audio --- */
void cr_app_render(int32_t *lr, uint32_t n)
{
    uint32_t i;
    float bpm;
    if (n > 128u)
        n = 128u;
    bpm = (cr_g.clock != 1u && cr_clock_seen && cr_clock_bpm > 0.f) ? cr_clock_bpm : cr_arp_bpm(&cr_p);
    if (bpm - cr_syn.tempo > 0.5f || cr_syn.tempo - bpm > 0.5f)
        cr_synth_tempo(&cr_syn, bpm);
    if (!(cr_g.clock != 1u && cr_clock_seen))
        cr_arp_run(&cr_arp, &cr_p, n);
    cr_synth_render(&cr_syn, cr_fbuf, n);
    for (i = 0; i < 2u * n; i++)
        lr[i] = (int32_t)(cr_fbuf[i] * 32767.f);
}

static void cr_note_in(uint32_t n, uint32_t v)   /* MIDI in, with the sustain pedal */
{
    if (!v && cr_sus) {
        cr_sus_bits[n >> 5] |= 1u << (n & 31u);
        return;
    }
    if (v)
        cr_sus_bits[n >> 5] &= ~(1u << (n & 31u));
    cr_play(n, v);
}

void cr_app_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t t = st & 0xF0u, ch = st & 0x0Fu;
    int p;
    if (cr_g.midi_in < 16u && ch != cr_g.midi_in)
        return;
    switch (t) {
    case 0x90u:
        cr_note_in(d1, d2 ? (cr_g.velo ? d2 : 100u) : 0u);
        break;
    case 0x80u:
        cr_note_in(d1, 0);
        break;
    case 0xE0u:
        cr_synth_bend(&cr_syn, (float)(((int32_t)d2 << 7 | d1) - 8192) * (float)cr_g.bend / 8192.f);
        break;
    case 0xD0u:
        cr_synth_ctl(&cr_syn, CR_S_AFTT, (float)d1 / 127.f);
        break;
    case 0xC0u:
        cr_want_preset = (int16_t)(d1 & 0x7Fu);   /* 0-63 the factory presets, 64-127 U1-1 .. U8-8 */
        break;
    case 0xB0u:
        if (d1 == 1u)
            cr_synth_ctl(&cr_syn, CR_S_MODW, (float)d2 / 127.f);
        else if (d1 == 11u)
            cr_synth_ctl(&cr_syn, CR_S_EXPR, (float)d2 / 127.f);
        else if (d1 == 2u && cr_g.breath)
            cr_synth_ctl(&cr_syn, cr_g.breath == 1u ? CR_S_MODW : cr_g.breath == 2u ? CR_S_AFTT : CR_S_EXPR, (float)d2 / 127.f);
        else if (d1 == 64u) {
            cr_sus = d2 >= 64u;
            if (!cr_sus) {
                uint32_t i;
                for (i = 0; i < 128u; i++)
                    if (cr_sus_bits[i >> 5] & (1u << (i & 31u)))
                        cr_play(i, 0);
                cr_sus_bits[0] = cr_sus_bits[1] = cr_sus_bits[2] = cr_sus_bits[3] = 0;
            }
        } else if (d1 == 120u || d1 == 123u) {
            cr_synth_all_off(&cr_syn);
            cr_arp_release_all(&cr_arp);
        } else if (d1 == 73u)
            cr_g.scale = (uint8_t)(d2 % CR_NSCALE);
        else if (d1 == 79u)
            cr_g.root = (uint8_t)(d2 % 12u);
        else if ((p = cr_param_of_cc(d1)) >= 0) {   /* a parameter, by the CRAFTsynth's CC chart */
            cr_p.p[p] = d2 & 0x7Fu;
            cr_synth_param(&cr_syn, (uint32_t)p, d2);
            if (p == CR_W1BANK || p == CR_W2BANK)
                cr_want_tables = 1;
            if (p == CR_ARP && d2 < 64u)
                cr_arp_release_all(&cr_arp);
            cr_dirty = 1;
            cr_want_draw = 1;
        }
        break;
    default:
        break;
    }
}

void cr_app_realtime(uint8_t b)
{
    uint32_t now = plat_ms();
    if (b == 0xF8u) {
        if (!cr_clock_seen || now - cr_clock_ms > 500u)
            cr_clock_n = 0;
        if (cr_clock_n == 0u)
            cr_clock_t0 = now;
        if (++cr_clock_n == 25u) {               /* a quarter note: 24 ticks */
            if (now > cr_clock_t0)
                cr_clock_bpm = 60000.f / (float)(now - cr_clock_t0);
            cr_clock_n = 1;
            cr_clock_t0 = now;
        }
        cr_clock_seen = 1;
        cr_clock_ms = now;
        if (cr_g.clock != 1u)
            cr_arp_clock(&cr_arp, &cr_p);
    } else if (b == 0xFAu) {
        cr_arp.ticks = 1000;                     /* start: the next tick is a step */
    }
}

void cr_app_panic(void)
{
    cr_lock();
    cr_arp_release_all(&cr_arp);
    cr_synth_all_off(&cr_syn);
    cr_unlock();
    cr_lat_n = 0;
    cr_midi3(0xB0u | cr_g.midi_ch, 123, 0);
}

void cr_app_init(void)
{
    uint32_t u;
    cr_synth_init(&cr_syn, cr_mem);
    cr_arp_init(&cr_arp, &cr_arp_out);
    if (plat_store_load(OBJ_SOUND, &cr_g, sizeof cr_g) != (int)sizeof cr_g || cr_globals_check(&cr_g))
        cr_globals_init(&cr_g);
    (void)u;
    cr_migrate();
    cr_load(cr_g.cur);
    cr_tables();
    cr_want_tables = 0;
    cr_touch();
}

#include "cr_editor.c"
