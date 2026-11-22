/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the patch. Every sound parameter is one byte, 0..127, and has the MIDI CC number the Modal CRAFTsynth
 * 2.0 uses for it (its manual's CC chart, OS 1.4), so a patch is literally a table of CC values: a CC in sets it, a
 * knob turned sends it. The few that the CRAFTsynth has no CC for (tempo, arp rate) use free CC numbers.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>

/* parameter ids (the patch's byte order) */
enum {
    CR_W1POS, CR_W2POS, CR_MIX, CR_OMAMT, CR_SPREAD, CR_W1BANK, CR_W2BANK, CR_OMMODE,
    CR_TUNE, CR_FINE, CR_OCT, CR_GLIDE,
    CR_CUT, CR_RES, CR_MORPH, CR_FEGAMT,
    CR_FA, CR_FD, CR_FS, CR_FR, CR_AA, CR_AD, CR_AS, CR_AR, CR_AEGAMT, CR_MA, CR_MD, CR_MS, CR_MR,
    CR_L1RATE, CR_L1SHAPE, CR_L1MODE, CR_L1SYNC, CR_L2RATE, CR_L2SHAPE, CR_L2MODE, CR_L2SYNC,
    CR_DIST, CR_DMIX, CR_DTIME, CR_DFB, CR_DSYNC, CR_VOL,
    CR_DEP0, CR_DEP1, CR_DEP2, CR_DEP3, CR_DEP4, CR_DEP5, CR_DEP6, CR_DEP7,   /* mod depth, source 0..7 */
    CR_DST0, CR_DST1, CR_DST2, CR_DST3, CR_DST4, CR_DST5, CR_DST6, CR_DST7,   /* mod destination, source 0..7 */
    CR_ARP, CR_TEMPO, CR_ARPRATE,
    CR_NP
};
#define CR_NP_MAX 64u
_Static_assert(CR_NP <= CR_NP_MAX, "the patch's parameter bytes");

/* the eight mod sources, one slot each (the CRAFTsynth's), in its CC chart's order */
enum { CR_S_LFO1, CR_S_LFO2, CR_S_MEG, CR_S_NOTE, CR_S_VELO, CR_S_MODW, CR_S_EXPR, CR_S_AFTT, CR_NSRC };

/* mod destinations (CR_DSTn values): 0 none, then 37 */
enum {
    CR_D_NONE, CR_D_W1, CR_D_W2, CR_D_MIX, CR_D_PITCH, CR_D_O2PITCH, CR_D_OMAMT, CR_D_L1RATE, CR_D_L2RATE,
    CR_D_L1SHAPE, CR_D_L2SHAPE, CR_D_CUT, CR_D_RES, CR_D_MORPH, CR_D_FEGAMT, CR_D_FA, CR_D_FD, CR_D_FS, CR_D_FR,
    CR_D_AEGAMT, CR_D_AA, CR_D_AD, CR_D_AS, CR_D_AR, CR_D_VOL, CR_D_MA, CR_D_MD, CR_D_MS, CR_D_MR, CR_D_DIST,
    CR_D_DMIX, CR_D_DTIME, CR_D_DFB, CR_D_SPREAD, CR_D_GLIDE, CR_D_L1DEP, CR_D_L2DEP, CR_D_MEGDEP, CR_ND
};

/* the 16 oscillator modifiers (CR_OMMODE: 0..127 in 16 bands of 8, as the CRAFTsynth's CC 41) */
enum { CR_OM_PM, CR_OM_WSYNC, CR_OM_RING, CR_OM_TFOLD, CR_OM_DEREZ, CR_OM_ROR, CR_OM_GLITCH, CR_OM_WAMP, CR_OM_FM,
       CR_OM_HSYNC, CR_OM_MIN, CR_OM_SFOLD, CR_OM_CRUSH, CR_OM_SCRUNCH, CR_OM_LOFI, CR_OM_VOCAL, CR_NOM };

#define CR_NBANK 9u                    /* factory banks: 8 on both waves, the 9th (sine / noise) on Wave 2 only */
#define CR_NUBANK 2u                   /* user banks (Serum import), on both waves: banks 10 and 11 */
#define CR_BANKS (CR_NBANK + CR_NUBANK)
#define CR_BANK_NOISE 8u

/* how a parameter is shown and stepped */
enum { CR_K_NUM, CR_K_BIP, CR_K_TIME, CR_K_BANK, CR_K_OMMODE, CR_K_SPREAD, CR_K_SEMI, CR_K_CENT, CR_K_OCT,
       CR_K_GLIDE, CR_K_HZ, CR_K_LSHAPE, CR_K_LMODE, CR_K_ONOFF, CR_K_DTIME, CR_K_DEST, CR_K_MORPH, CR_K_PCT,
       CR_K_TEMPO, CR_K_ARPRATE, CR_K_LRATE1, CR_K_LRATE2, CR_K_MIX };

typedef struct {
    uint8_t cc, kind, def;
    char name[9];                      /* the screen's label, capitals */
} cr_pinfo_t;
extern const cr_pinfo_t CR_P[CR_NP];
extern const char *const CR_DEST_NAME[CR_ND];
extern const char *const CR_SRC_NAME[CR_NSRC];
extern const char *const CR_OM_NAME[CR_NOM];
extern const char *const CR_BANK_NAME[CR_BANKS];

#define CR_NSTEP 16u                   /* the arp-seq's pattern */
#define CR_REST 0u                     /* a step's value: 0 a rest, else 64 + semitones from the pattern's root */
#define CR_PATCH_MAGIC 0xC5u
typedef struct {
    char name[12];
    uint8_t p[CR_NP_MAX];
    uint8_t steps, magic, ver, pad;
    uint8_t step[CR_NSTEP];
} cr_patch_t;
#define CR_PATCH_BYTES 96u
_Static_assert(sizeof(cr_patch_t) == CR_PATCH_BYTES, "a patch's bytes (the store, the editor)");

#define CR_NPRESET 64u                 /* 8 banks of 8, as the CRAFTsynth */

/* what is the FM-1's, not the patch's */
#define CR_NSCALE 16u
typedef struct {
    uint8_t magic, midi_ch, midi_in, scale, root, clock, bend, velo;
    uint8_t cur, local, breath, kbmode, contrast, arp_out, pad[2];
} cr_globals_t;
#define CR_GLOBALS_MAGIC 0xC6u
extern const char *const CR_SCALE_NAME[CR_NSCALE];
extern const uint16_t CR_SCALE_MASK[CR_NSCALE];   /* bit n: semitone n is in the scale */

void cr_patch_init(cr_patch_t *p, const char *name);   /* the CRAFTsynth's INIT: a plain saw */
int cr_patch_check(cr_patch_t *p);                     /* 0 = usable (values clamped); -1 = not a patch */
void cr_globals_init(cr_globals_t *g);
int cr_globals_check(cr_globals_t *g);
int cr_param_of_cc(uint32_t cc);                       /* -1 = no parameter has it */
uint32_t cr_om_of(uint32_t v);                         /* CR_OMMODE's value -> modifier 0..15 */
uint32_t cr_bank_of(uint32_t v, int wave2);            /* CR_W1BANK / CR_W2BANK's value -> bank */
uint32_t cr_bank_val(uint32_t bank, int wave2);        /* and back (the middle of its band) */
int32_t cr_semi_of(uint32_t v);                        /* CR_TUNE -> -48..48 semitones */
uint32_t cr_spread_shape(uint32_t v);                  /* CR_SPREAD: 0 unison (below 64), 1..9 the chords */
extern const char *const CR_SHAPE_NAME[10];
int cr_dest_ok(uint32_t src, uint32_t dst);            /* LFOs and the mod envelope don't modulate themselves */
