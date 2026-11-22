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
static int16_t cr_dly[CR_DLY_LEN] X0X_POOL;      /* the delay: 128 KB */
static cr_wtbank_t cr_tab[3] X0X_POOL;           /* banks ready to play: Wave 1's, Wave 2's, one being built */
static uint8_t cr_tab_bank[3] = {0xFF, 0xFF, 0xFF};
typedef struct {
    uint8_t magic, ver;
    char name[14];
    int16_t w[5][CR_WT_N];
} cr_ubank_t;                                    /* a user wavetable bank in flash (Serum import): 2,576 B */
#define CR_UBANK_MAGIC 0xC7u
static cr_ubank_t cr_ub[CR_NUBANK] X0X_POOL;
static uint8_t cr_obj[PLAT_STORE_MAX] X0X_POOL;  /* a store object being read or written */
static float cr_fbuf[2u * 128u];
static uint8_t cr_dirty;                         /* the working patch differs from its preset */
static volatile uint8_t cr_want_tables = 1, cr_want_draw;
static volatile int16_t cr_want_preset = -1;     /* a program change (from the interrupt) */
static volatile uint32_t cr_clock_n, cr_clock_t0, cr_clock_ms;
static volatile uint8_t cr_clock_seen;
static volatile float cr_clock_bpm;

enum { PG_OSC, PG_WAVES, PG_TUNE, PG_FILTER, PG_FEG, PG_AEG, PG_MEG, PG_LFO1, PG_LFO2, PG_MATRIX, PG_FX, PG_OUT,
       PG_ARP, PG_SEQ, PG_GLOB, PG_GLOB2, PG_GLOB3, PG_SAVE, PG_N };
static const char *const CR_PG_NAME[PG_N] = {"OSC", "WAVES", "TUNE", "FILTER", "FILTER EG", "AMP EG", "MOD EG",
    "LFO 1", "LFO 2", "MATRIX", "FX", "OUTPUT", "ARP", "SEQ", "MIDI", "KEYS", "SETUP", "SAVE"};
/* the page's four knobs: a patch parameter, or one of the G_ values (globals and page controls) */
enum { G_NONE = 100, G_MSRC, G_MDST, G_MDEP, G_STEPS, G_MCH, G_MIN, G_CLOCK, G_BEND, G_KEYS, G_SCALE, G_ROOT,
       G_VELO, G_LOCAL, G_BREATH, G_ARPOUT, G_SLOT, G_CPOS, G_CHAR };
static const uint8_t CR_PAGE_P[PG_N][4] = {
    {CR_W1POS, CR_W2POS, CR_MIX, CR_OMAMT},   {CR_W1BANK, CR_W2BANK, CR_OMMODE, CR_SPREAD},
    {CR_TUNE, CR_FINE, CR_OCT, CR_GLIDE},     {CR_CUT, CR_RES, CR_MORPH, CR_FEGAMT},
    {CR_FA, CR_FD, CR_FS, CR_FR},             {CR_AA, CR_AD, CR_AS, CR_AR},
    {CR_MA, CR_MD, CR_MS, CR_MR},             {CR_L1RATE, CR_L1SHAPE, CR_L1MODE, CR_L1SYNC},
    {CR_L2RATE, CR_L2SHAPE, CR_L2MODE, CR_L2SYNC}, {G_MSRC, G_MDST, G_MDEP, G_NONE},
    {CR_DIST, CR_DMIX, CR_DTIME, CR_DFB},     {CR_DSYNC, CR_VOL, CR_AEGAMT, CR_GLIDE},
    {CR_ARP, CR_TEMPO, CR_ARPRATE, G_STEPS},  {G_STEPS, CR_TEMPO, CR_ARPRATE, CR_ARP},
    {G_MCH, G_MIN, G_CLOCK, G_BEND},          {G_KEYS, G_SCALE, G_ROOT, G_VELO},
    {G_LOCAL, G_BREATH, G_ARPOUT, G_NONE},    {G_SLOT, G_NONE, G_CPOS, G_CHAR},
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
        const cr_ubank_t *u = &cr_ub[(b - CR_NBANK) % CR_NUBANK];
        return u->magic == CR_UBANK_MAGIC ? (const int16_t (*)[CR_WT_N])u->w : cr_wt_factory(0);
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
static void cr_ubank_load(uint32_t u)
{
    int n = plat_store_load(u ? OBJ_MOTION : OBJ_SONG, &cr_ub[u], sizeof cr_ub[u]);
    if (n != (int)sizeof cr_ub[u] || cr_ub[u].magic != CR_UBANK_MAGIC)
        cr_ub[u].magic = 0;
    cr_ub[u].name[sizeof cr_ub[u].name - 1u] = 0;
}

/* --------------------------------------------------------------------------------------------- presets --- */
static uint32_t cr_obj_of(uint32_t slot) { return slot < 32u ? OBJ_PAT0 : OBJ_PAT1; }
/* preset slot n as it is now: the saved one, else the factory one */
static void cr_preset_read(uint32_t slot, cr_patch_t *out)
{
    int n = plat_store_load(cr_obj_of(slot), cr_obj, sizeof cr_obj);
    uint32_t off = (slot % 32u) * CR_PATCH_BYTES, i;
    if (n >= (int)(off + CR_PATCH_BYTES)) {
        for (i = 0; i < CR_PATCH_BYTES; i++)
            ((uint8_t *)out)[i] = cr_obj[off + i];
        if (cr_patch_check(out) == 0)
            return;
    }
    for (i = 0; i < CR_PATCH_BYTES; i++)
        ((uint8_t *)out)[i] = CR_FACTORY[slot % CR_NPRESET][i];
    (void)cr_patch_check(out);
}
static int cr_preset_saved(uint32_t slot)
{
    int n = plat_store_load(cr_obj_of(slot), cr_obj, sizeof cr_obj);
    uint32_t off = (slot % 32u) * CR_PATCH_BYTES;
    return n >= (int)(off + CR_PATCH_BYTES) && cr_obj[off + 77u] == CR_PATCH_MAGIC;
}
/* slot n's bytes written (or, with p 0, cleared back to the factory preset) */
static int cr_preset_write(uint32_t slot, const cr_patch_t *p)
{
    int n = plat_store_load(cr_obj_of(slot), cr_obj, sizeof cr_obj);
    uint32_t off = (slot % 32u) * CR_PATCH_BYTES, i;
    for (i = n < 0 ? 0u : (uint32_t)n; i < 32u * CR_PATCH_BYTES; i++)
        cr_obj[i] = 0;
    for (i = 0; i < CR_PATCH_BYTES; i++)
        cr_obj[off + i] = p ? ((const uint8_t *)p)[i] : 0u;
    return plat_store_save(cr_obj_of(slot), cr_obj, 32u * CR_PATCH_BYTES);
}
static void cr_globals_save(void) { (void)plat_store_save(OBJ_SOUND, &cr_g, sizeof cr_g); }

static void cr_lat_clear(void);
static void cr_evt(uint32_t kind)               /* tell the editor (live sync): F0 7D 46 4C 48 kind preset dirty F7 */
{
    uint8_t ev[9] = {0xF0, 0x7D, 0x46, 0x4C, 48, 0, 0, 0, 0xF7};
    ev[5] = (uint8_t)(kind & 0x7Fu);
    ev[6] = cr_g.cur;
    ev[7] = cr_dirty;
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
static void cr_arp_on(uint32_t n, uint32_t v)
{
    cr_synth_on(&cr_syn, n, v);
    if (cr_g.arp_out)
        cr_midi3(0x90u | cr_g.midi_ch, n, v);
}
static void cr_arp_off(uint32_t n)
{
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
static const char *const CR_SHAPE_SHORT[10] = {"", "MAJ", "MIN", "MAJ6", "SUS4", "5TH", "5+8", "8+15", "8-8", "-8-15"};
static const char *const CR_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const CR_LSHAPE[5] = {"SINE", "TRI", "SAW", "SQUARE", "S+H"};
static const char *const CR_L1SYNC_S[16] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1", "3/2", "2", "3",
                                            "4", "6", "8", "12", "16"};
static const char *const CR_L2SYNC_S[8] = {"1/16", "1/8", "1/4", "1/2", "1", "5/4", "2", "4"};
static const char *const CR_L2ROOT_S[8] = {"R/8", "R/4", "R/2", "R", "Rx1.5", "Rx2", "Rx2.5", "Rx3"};
static const char *const CR_DSYNC_S[9] = {"1/16", "1/12", "1/8", "1/8D", "1/4", "1/2", "3/4", "4/4", "6/4"};

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
    default: return p < CR_NP ? CR_P[p].name : "";
    }
}

static void cr_time(char *b, float s)
{
    if (s < 1.f) {
        cr_num(b, (int32_t)(s * 1000.f + 0.5f), 0);
        cr_cat(b, "MS");
    } else {
        cr_dec(b, s, 1);
        cr_cat(b, "S");
    }
}

/* parameter p's value as text: short (a knob's cell) or long (shown big) */
static void cr_ptext(uint32_t p, char *b, int lng)
{
    uint32_t v = p < CR_NP ? cr_p.p[p] : 0u;
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
        case G_SLOT: b[0] = (char)('1' + cr_save_slot / 8u); b[1] = '-'; b[2] = (char)('1' + cr_save_slot % 8u); b[3] = 0; break;
        case G_CPOS: cr_num(b, (int32_t)cr_cpos + 1, 0); break;
        case G_CHAR: b[0] = cr_p.name[cr_cpos] ? cr_p.name[cr_cpos] : '_'; b[1] = 0; if (b[0] == ' ') cr_cpy(b, "SPC"); break;
        default: break;
        }
        return;
    }
    switch (CR_P[p].kind) {
    case CR_K_BIP: cr_num(b, (int32_t)v - 64, 1); break;
    case CR_K_MIX:
        if (v == 0u) cr_cpy(b, "W1");
        else if (v == 127u) cr_cpy(b, "W2");
        else cr_num(b, (int32_t)v, 0);
        break;
    case CR_K_TIME: { float t = (float)v / 127.f; cr_time(b, 0.001f + 4.f * t * t * t); break; }
    case CR_K_PCT: cr_num(b, (int32_t)(v * 100u + 63u) / 127, 0); cr_cat(b, "%"); break;
    case CR_K_BANK: {
        uint32_t bk = cr_bank_of(v, p == CR_W2BANK);
        if (lng) {
            cr_cpy(b, CR_BANK_NAME[bk]);
            if (bk >= CR_NBANK && cr_ub[bk - CR_NBANK].magic == CR_UBANK_MAGIC && cr_ub[bk - CR_NBANK].name[0]) {
                cr_cat(b, " ");
                cr_cat(b, cr_ub[bk - CR_NBANK].name);
                b[22] = 0;
            }
        } else if (bk >= CR_NBANK) {
            cr_cpy(b, "U");
            cr_num(b + 1, (int32_t)(bk - CR_NBANK + 1u), 0);
        } else {
            cr_cpy(b, "B");
            cr_num(b + 1, (int32_t)bk + 1, 0);
        }
        break;
    }
    case CR_K_OMMODE: cr_cpy(b, lng ? CR_OM_NAME[cr_om_of(v)] : CR_OM_SHORT[cr_om_of(v)]); break;
    case CR_K_SPREAD:
        if (v < 64u) {
            if (lng && v) { cr_cpy(b, "UNISON "); cr_num(b + 7, (int32_t)v, 0); }
            else if (lng) cr_cpy(b, "OFF");
            else cr_num(b, (int32_t)v, 0);
        } else
            cr_cpy(b, lng ? CR_SHAPE_NAME[cr_spread_shape(v)] : CR_SHAPE_SHORT[cr_spread_shape(v)]);
        break;
    case CR_K_SEMI: cr_num(b, cr_semi_of(v), 1); if (lng) cr_cat(b, " SEMI"); break;
    case CR_K_CENT: cr_num(b, ((int32_t)v - 64) * 100 / 63, 1); cr_cat(b, lng ? " CENTS" : "C"); break;
    case CR_K_OCT: cr_num(b, (int32_t)((v * 7u) >> 7) - 2, 1); break;
    case CR_K_GLIDE:
        if (v == 64u)
            cr_cpy(b, "OFF");
        else {
            float g = ((float)v - 64.f) / 63.f;
            if (g < 0.f)
                g = -g;
            if (lng) {
                cr_cpy(b, v > 64u ? "ALWAYS " : "LEGATO ");
                cr_time(b + 7, 2.5f * g * g);
            } else {
                cr_cpy(b, v > 64u ? "A" : "L");
                cr_num(b + 1, (int32_t)(g * 99.f + 0.5f), 0);
            }
        }
        break;
    case CR_K_HZ: {
        float f = 20.f * cr_exp2f((float)v * (10.f / 127.f));
        if (f < 1000.f)
            cr_num(b, (int32_t)(f + 0.5f), 0);
        else {
            cr_dec(b, f / 1000.f, 1);
            cr_cat(b, "K");
        }
        if (lng)
            cr_cat(b, "HZ");
        break;
    }
    case CR_K_MORPH:
        if (lng) {
            cr_cpy(b, v < 22u ? "LOW PASS" : v < 50u ? "LOW SHELF" : v < 78u ? "BAND PASS" : v < 106u ? "BP TO HP" : "HIGH PASS");
        } else
            cr_cpy(b, v < 22u ? "LP" : v < 50u ? "SHELF" : v < 78u ? "BP" : v < 106u ? "B-H" : "HP");
        break;
    case CR_K_LSHAPE: cr_cpy(b, CR_LSHAPE[(v * 4u + 63u) / 127u]); if (!lng && b[3]) b[3] = 0; break;
    case CR_K_LMODE: cr_cpy(b, v < 42u ? (lng ? "RETRIGGER" : "RTRIG") : v < 84u ? "FREE" : (lng ? "SINGLE" : "ONCE")); break;
    case CR_K_ONOFF: cr_cpy(b, v >= 64u ? "ON" : "OFF"); break;
    case CR_K_LRATE1:
        if (cr_p.p[CR_L1SYNC] >= 64u) {
            cr_cpy(b, CR_L1SYNC_S[(v * 16u) >> 7]);
            if (lng) cr_cat(b, " PER BAR");
        } else {
            float hz = 0.02f * cr_exp2f((float)v * (10.64f / 127.f));
            cr_dec(b, hz, hz < 10.f ? 2u : 1u);
            if (lng) cr_cat(b, " HZ");
        }
        break;
    case CR_K_LRATE2:
        if (v >= 64u)
            cr_cpy(b, CR_L2ROOT_S[((v - 64u) * 8u) >> 6]);
        else if (cr_p.p[CR_L2SYNC] >= 64u) {
            cr_cpy(b, CR_L2SYNC_S[(v * 8u) >> 6]);
            if (lng) cr_cat(b, " PER BEAT");
        } else {
            float hz = 0.02f * cr_exp2f((float)v * (2.f * 10.64f / 127.f));
            cr_dec(b, hz, hz < 10.f ? 2u : 1u);
            if (lng) cr_cat(b, " HZ");
        }
        break;
    case CR_K_DTIME:
        if (cr_p.p[CR_DSYNC] >= 64u)
            cr_cpy(b, CR_DSYNC_S[(v * 9u) >> 7]);
        else
            cr_time(b, 0.0005f * cr_exp2f((float)v * (10.55f / 127.f)));
        break;
    case CR_K_TEMPO: cr_num(b, 40 + 2 * (int32_t)v, 0); if (lng) cr_cat(b, " BPM"); break;
    case CR_K_ARPRATE: cr_cpy(b, CR_ARPRATE_NAME[(v * 6u) >> 7]); break;
    case CR_K_DEST: cr_cpy(b, CR_DEST_NAME[v % CR_ND]); break;
    default: cr_num(b, (int32_t)v, 0); break;
    }
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
    case G_SLOT: cr_save_slot = (uint32_t)lim32((int32_t)cr_save_slot + d, 0, CR_NPRESET - 1); break;
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
        CR_DFB, CR_SPREAD, CR_GLIDE, 0xFF, 0xFF, 0xFF};
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
    case B_HOME: cr_cycle(PG_OSC, PG_WAVES, PG_TUNE); break;
    case B_EDIT: cr_go(PG_FILTER); break;
    case B_ENV: cr_cycle(PG_FEG, PG_AEG, PG_MEG); break;
    case B_LFO: cr_cycle(PG_LFO1, PG_LFO2, PG_LFO2); break;
    case B_SEL: cr_go(PG_MATRIX); break;
    case B_FX: cr_cycle(PG_FX, PG_OUT, PG_OUT); break;
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
            cr_save_slot = cr_g.cur;
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
        if (cr_page == PG_FX || cr_page == PG_OUT) lit |= 1u << B_FX;
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
/* light, high contrast, big type (the values 23 px, the one turned 44 px) */
#define C_BG RGB(246, 244, 239)
#define C_BAND RGB(226, 222, 212)
#define C_INK RGB(12, 14, 16)
#define C_SOFT RGB(70, 76, 84)
#define C_ACC RGB(0, 90, 170)
#define C_RULE RGB(190, 186, 176)
#define C_HIL RGB(255, 205, 70)
#define C_GRID RGB(214, 210, 200)

static void cr_center(int32_t x, int32_t w, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(x + (w - text_w(f, s)) / 2, y, f, s, c);
}
static const felucca_font_t *cr_fit(const char *s, int32_t w)
{
    if (text_w(&FONT_M, s) <= w)
        return &FONT_M;
    if (text_w(&FONT_B, s) <= w)
        return &FONT_B;
    return &FONT_S;
}
static void cr_thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
    cv_line(x0 + 1, y0, x1 + 1, y1, c);
}

/* a wave of a bank at a morph position, drawn into the box */
static void cr_draw_wave(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bank, uint32_t pos, uint16_t c)
{
    const int16_t (*b)[CR_WT_N] = cr_bank_base(bank);
    float m = (float)pos * (4.f / 127.f), f, sc = (float)h * 0.45f / 26000.f;
    uint32_t wa = (uint32_t)m, i;
    int32_t py = 0;
    if (wa > 3u)
        wa = 3u;
    f = m - (float)wa;
    for (i = 0; i <= (uint32_t)w; i++) {
        uint32_t k = i * 255u / (uint32_t)w;
        float v = (float)b[wa][k] + ((float)b[wa + 1u][k] - (float)b[wa][k]) * f;
        int32_t yy = y + h / 2 - (int32_t)(v * sc);
        if (bank == CR_BANK_NOISE && pos > 0u)  /* the noise bank: a sine going to noise */
            yy += (int32_t)((float)((k * 2654435761u) >> 26) - 32.f) * (float)pos * (float)h / 9000.f;
        if (i)
            cr_thick(x + (int32_t)i - 1, py, x + (int32_t)i, yy, c);
        py = yy;
    }
}

static void cr_draw_env(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t a)
{
    float t[4], tot, sx;
    uint32_t i;
    int32_t x0 = x, x1, x2, x3, x4, ys;
    for (i = 0; i < 4u; i++) {
        float v = (float)cr_p.p[a + i] / 127.f;
        t[i] = i == 2u ? 0.6f : 0.05f + 4.f * v * v * v;
    }
    tot = t[0] + t[1] + t[2] + t[3];
    sx = (float)w / tot;
    x1 = x0 + (int32_t)(t[0] * sx);
    x2 = x1 + (int32_t)(t[1] * sx);
    x3 = x2 + (int32_t)(t[2] * sx);
    x4 = x + w;
    ys = y + h - (int32_t)((float)cr_p.p[a + 2u] / 127.f * (float)h);
    cr_thick(x0, y + h, x1, y, C_INK);
    cr_thick(x1, y, x2, ys, C_INK);
    cr_thick(x2, ys, x3, ys, C_INK);
    cr_thick(x3, ys, x4, y + h, C_INK);
}

static void cr_draw_lfo(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t sh)
{
    float s = (float)cr_p.p[sh] / 127.f * 4.f, f;
    uint32_t k = (uint32_t)s, i, r = 0x1234u;
    int32_t py = 0;
    float hold = 0.f;
    if (k > 3u)
        k = 3u;
    f = s - (float)k;
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
        yy = y + h / 2 - (int32_t)(val * (float)h * 0.45f);
        if (i)
            cr_thick(x + (int32_t)i - 1, py, x + (int32_t)i, yy, C_INK);
        py = yy;
    }
}

static void cr_draw_filter(int32_t x, int32_t y, int32_t w, int32_t h)
{
    float fc = (float)cr_p.p[CR_CUT] / 127.f * 10.f, k = 2.f * (1.f - 0.985f * (float)cr_p.p[CR_RES] / 127.f);
    float m = (float)cr_p.p[CR_MORPH] / 127.f, wl = m < 0.5f ? 1.f - 2.f * m : 0.f, wh = m < 0.5f ? 0.f : 2.f * m - 1.f;
    float wb = m < 0.5f ? 2.f * m : 2.f - 2.f * m;
    uint32_t i;
    int32_t py = 0;
    for (i = 0; i <= (uint32_t)w; i++) {
        float oc = (float)i / (float)w * 10.f, r = cr_exp2f(oc - fc), r2 = r * r;
        float re = 1.f - r2, im = k * r, den = re * re + im * im, hr, hi, mag, db;
        /* H = (wl + wb j r + wh (-r^2)) / (1 - r^2 + j k r) */
        float nr = wl - wh * r2, ni = wb * r;
        hr = (nr * re + ni * im) / den;
        hi = (ni * re - nr * im) / den;
        mag = hr * hr + hi * hi;
        db = 10.f * cr_log2f(mag + 1e-9f) * 0.30103f;
        db = db < -36.f ? -36.f : db > 18.f ? 18.f : db;
        {
            int32_t yy = y + (int32_t)((18.f - db) / 54.f * (float)h);
            if (i)
                cr_thick(x + (int32_t)i - 1, py, x + (int32_t)i, yy, C_INK);
            py = yy;
        }
    }
    cv_line(x, y + h * 18 / 54, x + w, y + h * 18 / 54, C_RULE);
}

void cr_app_draw(int force)
{
    char b[40], t[16];
    uint32_t i;
    if (!force && cr_drawn == cr_ver)
        return;
    cr_drawn = cr_ver;
    /* ---- top: the preset, its name, the page; the modifier and what is on ---- */
    cv_begin(240, 120, C_BG);
    cv_rect(0, 0, 240, 34, C_BAND);
    b[0] = (char)('1' + cr_g.cur / 8u);
    b[1] = '-';
    b[2] = (char)('1' + cr_g.cur % 8u);
    b[3] = 0;
    cv_text(5, 5, &FONT_M, b, C_ACC);
    for (i = 0; i < 11u && cr_p.name[i]; i++)
        b[i] = cr_p.name[i];
    b[i] = 0;
    if (cr_dirty)
        cr_cat(b, "*");
    cv_text(44, 5, cr_fit(b, 120), b, C_INK);
    cv_text(236 - text_w(&FONT_B, CR_PG_NAME[cr_page]), 10, &FONT_B, CR_PG_NAME[cr_page], C_ACC);
    cv_rect(0, 34, 240, 2, C_INK);
    cr_cpy(b, CR_OM_NAME[cr_om_of(cr_p.p[CR_OMMODE])]);
    cv_text(5, 40, &FONT_B, b, C_INK);
    t[0] = 0;
    if (cr_p.p[CR_ARP] >= 64u)
        cr_cat(t, cr_p.steps ? "SEQ " : "ARP ");
    if (cr_latch)
        cr_cat(t, "LATCH ");
    if (cr_oct) {
        cr_cat(t, "O");
        cr_num(t + cr_len(t), cr_oct, 1);
    }
    cv_text(236 - text_w(&FONT_B, t), 40, &FONT_B, t, C_ACC);
    /* ---- middle (60..158): what was just turned, big; else the page's picture ---- */
    if (cr_msg[0]) {
        cv_rect(0, 64, 240, 56, C_HIL);
        cr_center(0, 240, 70, cr_fit(cr_msg, 236), cr_msg, C_INK);
    } else if (cr_hit >= 0) {
        cr_center(0, 240, 62, &FONT_M, cr_plabel(cr_hit_p), C_SOFT);
        cr_ptext(cr_hit_p, b, 1);
        cr_center(0, 240, 86, text_w(&FONT_XL, b) <= 236 ? &FONT_XL : &FONT_M, b, C_INK);
    } else if (cr_page <= PG_TUNE) {
        cv_text(6, 62, &FONT_S, "WAVE 1", C_SOFT);
        cv_text(126, 62, &FONT_S, "WAVE 2", C_ACC);
        cr_draw_wave(6, 76, 108, 44, cr_bank_of(cr_p.p[CR_W1BANK], 0), cr_p.p[CR_W1POS], C_INK);
        cr_draw_wave(126, 76, 108, 44, cr_bank_of(cr_p.p[CR_W2BANK], 1), cr_p.p[CR_W2POS], C_ACC);
    } else if (cr_page >= PG_FEG && cr_page <= PG_MEG) {
        cr_draw_env(10, 64, 220, 54, CR_PAGE_P[cr_page][0]);
    } else if (cr_page == PG_LFO1 || cr_page == PG_LFO2) {
        cr_draw_lfo(10, 62, 150, 56, cr_page == PG_LFO1 ? CR_L1SHAPE : CR_L2SHAPE);
        {
            uint32_t s = cr_page == PG_LFO1 ? 0u : 1u, dst = cr_p.p[CR_DST0 + s];
            cv_text(168, 64, &FONT_S, "TO", C_SOFT);
            cv_text(168, 80, cr_fit(CR_DEST_NAME[dst], 70), CR_DEST_NAME[dst], C_INK);
            if (dst) {
                cr_num(b, (int32_t)cr_p.p[CR_DEP0 + s] - 64, 1);
                cv_text(168, 100, &FONT_B, b, C_ACC);
            }
        }
    } else if (cr_page == PG_FILTER) {
        cr_draw_filter(10, 62, 220, 56);
    } else if (cr_page == PG_MATRIX) {
        cr_center(0, 240, 62, &FONT_M, CR_SRC_NAME[cr_msrc], C_ACC);
        cr_cpy(b, "TO ");
        cr_cat(b, CR_DEST_NAME[cr_p.p[CR_DST0 + cr_msrc] % CR_ND]);
        cr_center(0, 240, 90, &FONT_M, b, C_INK);
    } else if (cr_page == PG_SEQ || (cr_page == PG_ARP && cr_p.steps)) {
        for (i = 0; i < CR_NSTEP; i++) {
            int32_t x = 4 + (int32_t)(i % 8u) * 29, y = 62 + (int32_t)(i / 8u) * 28;
            if (i < cr_p.steps) {
                uint32_t s = cr_p.step[i];
                cv_rect(x, y, 27, 26, s == CR_REST ? C_GRID : C_INK);
                if (s != CR_REST) {
                    cr_num(t, (int32_t)s - 64, 0);
                    cr_center(x, 27, y + 5, &FONT_B, t, C_BG);
                }
            } else
                cv_rect(x, y, 27, 26, C_GRID);
        }
    } else if (cr_page == PG_SAVE) {
        cr_center(0, 240, 60, &FONT_S, cr_preset_saved(cr_save_slot) ? "OVERWRITES YOUR PRESET" : "OVER THE FACTORY PRESET", C_SOFT);
        for (i = 0; i < 11u; i++) {
            int32_t x = 10 + (int32_t)i * 20;
            char c[2] = {cr_p.name[i] ? cr_p.name[i] : ' ', 0};
            if (i == cr_cpos)
                cv_rect(x, 80, 19, 30, C_HIL);
            cv_rect(x, 110, 19, 2, C_INK);
            cr_center(x, 19, 82, &FONT_M, c, C_INK);
        }
    } else {
        cr_ptext(CR_PAGE_P[cr_page][0], b, 1);
        cr_center(0, 240, 62, &FONT_M, cr_plabel(CR_PAGE_P[cr_page][0]), C_SOFT);
        cr_center(0, 240, 86, text_w(&FONT_XL, b) <= 236 ? &FONT_XL : &FONT_M, b, C_INK);
    }
    cv_blit(0, 0);
    /* ---- bottom: the four knobs, big ---- */
    cv_begin(240, 120, C_BG);
    if (cr_page == PG_SAVE) {
        cr_center(0, 240, 2, &FONT_B, "SAVE: SAVE   PLAY: INIT", C_SOFT);
    }
    cv_rect(0, 38, 240, 2, C_INK);
    for (i = 0; i < 4u; i++) {
        uint32_t p = CR_PAGE_P[cr_page][i];
        int32_t x = (int32_t)i * 60;
        const char *l;
        if (i)
            cv_rect(x, 40, 1, 80, C_RULE);
        if (p == G_NONE)
            continue;
        if ((int32_t)i == cr_hit)
            cv_rect(x + 1, 40, 59, 80, C_HIL);
        l = cr_plabel(p);
        cr_center(x, 60, 48, text_w(&FONT_S, l) <= 58 ? &FONT_S : &FONT_XS, l, C_SOFT);
        cr_ptext(p, b, 0);
        cr_center(x, 60, 74, cr_fit(b, 58), b, C_INK);
    }
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
        cr_want_preset = (int16_t)(d1 % CR_NPRESET);
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
    cr_synth_init(&cr_syn, cr_dly);
    cr_arp_init(&cr_arp, &cr_arp_out);
    if (plat_store_load(OBJ_SOUND, &cr_g, sizeof cr_g) != (int)sizeof cr_g || cr_globals_check(&cr_g))
        cr_globals_init(&cr_g);
    for (u = 0; u < CR_NUBANK; u++)
        cr_ubank_load(u);
    cr_load(cr_g.cur);
    cr_tables();
    cr_want_tables = 0;
    cr_touch();
}

#include "cr_editor.c"
