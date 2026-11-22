/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the web editor's commands (docs/PROTOCOL.md), in Felucca's editor frames F0 7D 46 4C cmd args F7.
 * Included by cr_app.c (its statics). Parameter edits need no command: every parameter is a MIDI CC (the
 * CRAFTsynth's chart and free numbers for the rest), and the FM-1 sends the CC of every knob turned, so the editor
 * and the panel follow each other. Protocol 2 (0.4): preset slots are two 7-bit bytes (0..191; 0x3FFF the working
 * patch), patches are 128 bytes, user banks 0..31 live in flash. Copyright (C) 2026 Jamie (TwistedGenetics). */
enum { CR_ED_INFO = 40, CR_ED_PGET, CR_ED_PPUT, CR_ED_SELECT, CR_ED_GGET, CR_ED_GPUT, CR_ED_WPUT, CR_ED_WCOMMIT,
       CR_ED_EVT /* 48: sent by the FM-1, never asked for */, CR_ED_WGET, CR_ED_PRESET_RESET };
#define CR_ED_WORKING 0x3FFFu
#define CR_ED_PROTO 2u
#define CR_ED_CHUNK 256u

int cr_ed_long(uint32_t cmd) { return cmd == CR_ED_PPUT || cmd == CR_ED_GPUT || cmd == CR_ED_WPUT; }

static void cr_ed_pack(void (*put)(uint32_t), const uint8_t *b, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 7u) {
        uint32_t m = 0, k = n - i < 7u ? n - i : 7u;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(b[i + j] >> 7) << j;
        put(m);
        for (j = 0; j < k; j++)
            put(b[i + j] & 0x7Fu);
    }
}
static uint32_t cr_ed_unpack(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
static uint32_t cr_ed_slot(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 7; }
static void cr_ed_put_slot(void (*put)(uint32_t), uint32_t s)
{
    put(s & 0x7Fu);
    put((s >> 7) & 0x7Fu);
}

int cr_ed_cmd(uint32_t cmd, const uint8_t *a, uint32_t na, void (*put)(uint32_t))
{
    static cr_patch_t t;
    uint32_t i, s;
    switch (cmd) {
    case CR_ED_INFO: {
        const char *v = "Crafty Synth " CR_VERSION;
        for (i = 0; v[i] && i < 28u; i++)
            put((uint8_t)v[i] & 0x7Fu);
        put(0);
        put(CR_ED_PROTO);
        put(CR_NFACTORY);
        put(CR_NUSER & 0x7Fu);
        put(CR_NUSER >> 7);
        cr_ed_put_slot(put, cr_g.cur);
        put(cr_dirty);
        put(CR_NUBANK);
        for (i = 0; i < CR_NUBANK; i++)
            put(cr_ubank(i) != 0);
        return 1;
    }
    case CR_ED_PGET:
        if (na < 2u)
            return 0;
        s = cr_ed_slot(a);
        if (s == CR_ED_WORKING)
            t = cr_p;
        else if (s < CR_NPRESET)
            cr_preset_read(s, &t);
        else
            return 0;
        cr_ed_put_slot(put, s);
        put(s < CR_NPRESET ? (uint32_t)cr_preset_saved(s) : (uint32_t)cr_dirty);
        cr_ed_pack(put, (const uint8_t *)&t, CR_PATCH_BYTES);
        return 1;
    case CR_ED_PPUT:
        if (na < 3u)
            return 0;
        s = cr_ed_slot(a);
        if (cr_ed_unpack(a + 2, na - 2u, (uint8_t *)&t, CR_PATCH_BYTES) != CR_PATCH_BYTES || cr_patch_check(&t))
            return 0;
        cr_ed_put_slot(put, s);
        if (s == CR_ED_WORKING) {
            cr_use(&t);
            cr_dirty = 1;
            put(0);
        } else if (cr_is_user(s)) {
            cr_app_panic();
            put(cr_preset_write(s, &t) == 0 ? 0u : 1u);
            if (s == cr_g.cur && !cr_dirty)
                cr_use(&t);
            cr_evt(2);
        } else
            put(2);                              /* the factory presets can't be written */
        return 1;
    case CR_ED_SELECT:
        if (na < 2u || (s = cr_ed_slot(a)) >= CR_NPRESET)
            return 0;
        cr_load(s);
        cr_ed_put_slot(put, s);
        return 1;
    case CR_ED_PRESET_RESET:                     /* a user slot emptied */
        if (na < 2u || !cr_is_user(s = cr_ed_slot(a)))
            return 0;
        cr_app_panic();
        cr_ed_put_slot(put, s);
        put(cr_preset_write(s, 0) == 0 ? 0u : 1u);
        return 1;
    case CR_ED_GGET:
        cr_ed_pack(put, (const uint8_t *)&cr_g, sizeof cr_g);
        return 1;
    case CR_ED_GPUT: {
        static cr_globals_t g;
        if (cr_ed_unpack(a, na, (uint8_t *)&g, sizeof g) != sizeof g || cr_globals_check(&g))
            return 0;
        g.cur = cr_g.cur;
        g.pad[0] = cr_g.pad[0];
        cr_g = g;
        cr_globals_save();
        cr_touch();
        put(0);
        return 1;
    }
    case CR_ED_WPUT: {                           /* a user bank, 256 bytes at a time, into RAM */
        uint32_t u, c, n, off;
        if (na < 3u)
            return 0;
        u = a[0];
        c = a[1];
        off = c * CR_ED_CHUNK;
        if (u >= CR_NUBANK || off >= sizeof(cr_ubank_t))
            return 0;
        n = sizeof(cr_ubank_t) - off < CR_ED_CHUNK ? sizeof(cr_ubank_t) - off : CR_ED_CHUNK;
        if (cr_ed_unpack(a + 2, na - 2u, (uint8_t *)&cr_ubw + off, n) != n)
            return 0;
        put(u);
        put(c);
        return 1;
    }
    case CR_ED_WCOMMIT: {                        /* checked, written to flash, and rebuilt if a wave plays it */
        uint32_t u, k;
        int r;
        if (na < 1u || a[0] >= CR_NUBANK)
            return 0;
        u = a[0];
        put(u);
        if (cr_ubw.magic != CR_UBANK_MAGIC || cr_ubw.ver != 1u) {
            put(2);
            return 1;
        }
        cr_ubw.name[sizeof cr_ubw.name - 1u] = 0;
        cr_app_panic();
        cr_lock();
        cr_synth_tables(&cr_syn, 0, 0, 0);       /* nothing reads a table while its bank changes */
        cr_unlock();
        for (k = 0; k < 3u; k++)
            if (cr_tab_bank[k] == CR_NBANK + u)
                cr_tab_bank[k] = 0xFF;
        r = cr_ubank_write(u, &cr_ubw);
        cr_want_tables = 1;                      /* (the main loop rebuilds what the patch plays) */
        put(r == 0 ? 0u : 1u);
        return 1;
    }
    case CR_ED_WGET: {
        uint32_t u, c, n, off;
        const cr_ubank_t *b;
        if (na < 2u || a[0] >= CR_NUBANK)
            return 0;
        u = a[0];
        c = a[1];
        off = c * CR_ED_CHUNK;
        if (off >= sizeof(cr_ubank_t))
            return 0;
        b = cr_ubank(u);
        n = sizeof(cr_ubank_t) - off < CR_ED_CHUNK ? sizeof(cr_ubank_t) - off : CR_ED_CHUNK;
        put(u);
        put(c);
        if (b)
            cr_ed_pack(put, (const uint8_t *)b + off, n);
        return 1;
    }
    default:
        return 0;
    }
}
