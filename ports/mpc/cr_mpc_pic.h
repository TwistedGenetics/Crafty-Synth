/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth on the MPC (0.20, an experiment): a wavetable file's 3D view, drawn on the MPC as the factory banks' are
 * drawn at build time (make_views.py, the editor's drawTable): the file's five waves morphed, 15 of them stacked in
 * depth, the one at WT POS bright and filled, 17 frames for WT POS. Written as a PNG over the skin's own picture for
 * it (Plugin Skins/sh_crafty_file<n>.png, named by skin_post.py), in a thread of its own. Whether MPC shows it at once,
 * once the plugin is reopened, or never (it may keep the skin it loaded) is what this finds out.
 * Included by cr_mpc.c. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <pthread.h>
#include <math.h>

#define PIC_W 560
#define PIC_H 180
#define PIC_FRAMES 17
#define PIC_SS 2                                /* drawn at twice the size, then averaged down */

/* ---- a PNG, truecolour, its pixels stored (no compression: no zlib here) ---- */
static uint32_t PNG_CRC[256];
static void png_crc_init(void)
{
    uint32_t n, k, c;
    for (n = 0; n < 256u; n++) {
        for (c = n, k = 0; k < 8u; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        PNG_CRC[n] = c;
    }
}
static uint32_t png_crc(uint32_t c, const uint8_t *b, size_t n)
{
    while (n--)
        c = PNG_CRC[(c ^ *b++) & 255u] ^ (c >> 8);
    return c;
}
static void be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}
static int png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    uint32_t c;
    be32(h, n);
    memcpy(h + 4, type, 4);
    c = png_crc(0xFFFFFFFFu, h + 4, 4);
    c = png_crc(c, d, n) ^ 0xFFFFFFFFu;
    if (fwrite(h, 1, 8, f) != 8 || (n && fwrite(d, 1, n, f) != n))
        return 0;
    be32(h, c);
    return fwrite(h, 1, 4, f) == 4;
}
/* rgb: w x h x 3 bytes; written to path through a temporary file (MPC never sees half of it) */
static int png_write(const char *path, const uint8_t *rgb, uint32_t w, uint32_t h)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint32_t row = 1u + 3u * w, raw = row * h, nblk = (raw + 65534u) / 65535u, y, a = 1, b = 0;
    size_t zn = 2u + (size_t)nblk * 5u + raw + 4u, o = 0, left = raw;
    uint8_t *z = malloc(zn), ih[13];
    char tmp[1200];
    FILE *f;
    int ok;
    if (!z)
        return 0;
    z[o++] = 0x78;                              /* zlib: deflate, no dictionary */
    z[o++] = 0x01;
    for (y = 0; y < h; y++) {                   /* each row: filter 0, then its pixels */
        const uint8_t *p = rgb + (size_t)y * 3u * w;
        uint32_t k;
        for (k = 0; k < row; k++) {
            uint8_t v = k ? p[k - 1u] : 0u;
            if (((size_t)y * row + k) % 65535u == 0u) {   /* a stored block's header */
                uint32_t len = left < 65535u ? (uint32_t)left : 65535u;
                z[o++] = left <= 65535u ? 1u : 0u;
                z[o++] = (uint8_t)len;
                z[o++] = (uint8_t)(len >> 8);
                z[o++] = (uint8_t)~len;
                z[o++] = (uint8_t)(~len >> 8);
                left -= len;
            }
            z[o++] = v;
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
        }
    }
    be32(z + o, (b << 16) | a);
    o += 4;
    be32(ih, w);
    be32(ih + 4, h);
    ih[8] = 8;                                  /* 8 bits, RGB, deflate, no filter choice, not interlaced */
    ih[9] = 2;
    ih[10] = ih[11] = ih[12] = 0;
    snprintf(tmp, sizeof tmp, "%s.part", path);
    if (!(f = fopen(tmp, "wb"))) {
        free(z);
        return 0;
    }
    ok = fwrite(sig, 1, 8, f) == 8 && png_chunk(f, "IHDR", ih, 13) && png_chunk(f, "IDAT", z, (uint32_t)o) &&
         png_chunk(f, "IEND", 0, 0);
    ok = fclose(f) == 0 && ok;
    free(z);
    return ok && rename(tmp, path) == 0;
}

/* ---- the drawing (make_views.py's frame(), in C) ---- */
typedef struct {
    float *r, *g, *b, *cov;                     /* the frame at PIC_SS, and one layer's coverage */
} pic_t;
#define PW (PIC_W * PIC_SS)
#define PH (PIC_H * PIC_SS)
static void pic_dot(pic_t *p, float x, float y, float rad)   /* coverage: a soft disc */
{
    int x0 = (int)(x - rad - 1.f), x1 = (int)(x + rad + 1.f), y0 = (int)(y - rad - 1.f), y1 = (int)(y + rad + 1.f), i, j;
    for (j = y0 < 0 ? 0 : y0; j <= y1 && j < PH; j++)
        for (i = x0 < 0 ? 0 : x0; i <= x1 && i < PW; i++) {
            float dx = (float)i + 0.5f - x, dy = (float)j + 0.5f - y, d = sqrtf(dx * dx + dy * dy), c = rad + 0.5f - d;
            c = c > 1.f ? 1.f : c;
            if (c > p->cov[j * PW + i])
                p->cov[j * PW + i] = c;
        }
}
static void pic_line(pic_t *p, float xa, float ya, float xb, float yb, float rad)
{
    float dx = xb - xa, dy = yb - ya, len = sqrtf(dx * dx + dy * dy);
    int n = (int)(len * 2.f) + 1, i;
    for (i = 0; i <= n; i++)
        pic_dot(p, xa + dx * (float)i / (float)n, ya + dy * (float)i / (float)n, rad);
}
static void pic_paint(pic_t *p, const uint8_t col[3], float alpha)   /* the layer's coverage, in col, then cleared */
{
    int i;
    for (i = 0; i < PW * PH; i++)
        if (p->cov[i] > 0.f) {
            float a = p->cov[i] * alpha;
            p->r[i] += ((float)col[0] - p->r[i]) * a;
            p->g[i] += ((float)col[1] - p->g[i]) * a;
            p->b[i] += ((float)col[2] - p->b[i]) * a;
            p->cov[i] = 0.f;
        }
}
static float pic_wave(const int16_t (*w)[CR_WT_N], float pos, float u)  /* the 5 waves morphed at pos, at u in 0..1 */
{
    float m = pos * 4.f, f;
    int a = m >= 4.f ? 3 : (int)m, k = (int)(u * 256.f);
    k = k > 255 ? 255 : k;
    f = m - (float)a;
    return ((float)w[a][k] + ((float)w[a + 1][k] - (float)w[a][k]) * f) / 26000.f;
}
/* the strip: PIC_FRAMES frames of PIC_W x PIC_H, RGB, into out */
static void pic_draw(const int16_t (*w)[CR_WT_N], const uint8_t col[3], uint8_t *out, pic_t *p)
{
    static const uint8_t BGC[3] = {0x14, 0x17, 0x1B}, LINEC[3] = {0x34, 0x3A, 0x42};
    const int F = 15;
    const float dx = PW * 0.018f, dy = PH * 0.035f, fw = PW - 16 * PIC_SS - dx * (F - 1),
                amp = (PH - 20 * PIC_SS - dy * (F - 1)) * 0.45f, x0 = 8 * PIC_SS, base = PH - 10 * PIC_SS - amp;
    int n, f, i, x, y;
    for (n = 0; n < PIC_FRAMES; n++) {
        float pos = (float)n / (float)(PIC_FRAMES - 1), sx = x0 + pos * dx * (F - 1), sy = base - pos * dy * (F - 1);
        for (i = 0; i < PW * PH; i++) {
            p->r[i] = BGC[0];
            p->g[i] = BGC[1];
            p->b[i] = BGC[2];
            p->cov[i] = 0.f;
        }
        for (f = F - 1; f >= 0; f--) {          /* the stack, faint, the back first */
            float ox = x0 + (float)f * dx, oy = base - (float)f * dy, px = ox, py = oy - pic_wave(w, (float)f / (F - 1), 0.f) * amp;
            for (x = 2 * PIC_SS; x <= (int)fw; x += 2 * PIC_SS) {
                float qx = ox + (float)x, qy = oy - pic_wave(w, (float)f / (F - 1), (float)x / fw) * amp;
                pic_line(p, px, py, qx, qy, 0.5f * PIC_SS);
                px = qx;
                py = qy;
            }
            pic_paint(p, col, 0.14f + 0.32f * (1.f - (float)f / F));
        }
        for (x = 0; x <= (int)fw; x++) {         /* the one playing: filled */
            float yy = sy - pic_wave(w, pos, (float)x / fw) * amp;
            int ya = (int)(yy < sy ? yy : sy), yb = (int)(yy < sy ? sy : yy);
            for (y = ya < 0 ? 0 : ya; y <= yb && y < PH; y++)
                p->cov[y * PW + (int)(sx + (float)x)] = 1.f;
        }
        pic_paint(p, col, 0.28f);
        for (i = 0; i < 2; i++) {               /* its glow, then its line */
            float px = sx, py = sy - pic_wave(w, pos, 0.f) * amp;
            for (x = PIC_SS; x <= (int)fw; x += PIC_SS) {
                float qx = sx + (float)x, qy = sy - pic_wave(w, pos, (float)x / fw) * amp;
                pic_line(p, px, py, qx, qy, i ? 1.25f * PIC_SS : 4.f * PIC_SS);
                px = qx;
                py = qy;
            }
            pic_paint(p, col, i ? 1.f : 0.22f);
        }
        for (y = 0; y < PIC_H; y++)             /* averaged down, the frame's border on top */
            for (x = 0; x < PIC_W; x++) {
                uint8_t *o = out + (((size_t)n * PIC_H + (size_t)y) * PIC_W + (size_t)x) * 3u;
                float r = 0, g = 0, b = 0;
                int a, c;
                for (a = 0; a < PIC_SS; a++)
                    for (c = 0; c < PIC_SS; c++) {
                        int q = (y * PIC_SS + a) * PW + x * PIC_SS + c;
                        r += p->r[q];
                        g += p->g[q];
                        b += p->b[q];
                    }
                if (x == 0 || y == 0 || x == PIC_W - 1 || y == PIC_H - 1) {
                    o[0] = LINEC[0];
                    o[1] = LINEC[1];
                    o[2] = LINEC[2];
                } else {
                    o[0] = (uint8_t)(r / (PIC_SS * PIC_SS) + 0.5f);
                    o[1] = (uint8_t)(g / (PIC_SS * PIC_SS) + 0.5f);
                    o[2] = (uint8_t)(b / (PIC_SS * PIC_SS) + 0.5f);
                }
            }
    }
}

/* ---- the thread: the newest request per oscillator drawn and written ---- */
static pthread_mutex_t PIC_LOCK = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t PIC_WAKE = PTHREAD_COND_INITIALIZER;
static int PIC_RUNNING;
static struct {
    int16_t w[5][CR_WT_N];
    char path[1200];
    uint32_t gen, tried, ok;                    /* requested, last drawn, last written */
} PIC_JOB[2];
static void *pic_thread(void *arg)
{
    pic_t p;
    uint8_t *rgb = malloc((size_t)PIC_W * PIC_H * PIC_FRAMES * 3u);
    (void)arg;
    p.r = malloc(sizeof(float) * PW * PH);
    p.g = malloc(sizeof(float) * PW * PH);
    p.b = malloc(sizeof(float) * PW * PH);
    p.cov = malloc(sizeof(float) * PW * PH);
    if (!rgb || !p.r || !p.g || !p.b || !p.cov)
        return 0;
    png_crc_init();
    for (;;) {
        static const uint8_t COL[2][3] = {{0x3F, 0xA9, 0xF5}, {0x7E, 0xDE, 0xC8}};
        int16_t w[5][CR_WT_N];
        char path[1200];
        uint32_t gen = 0;
        int k = -1, j;
        pthread_mutex_lock(&PIC_LOCK);
        while (k < 0) {
            for (j = 0; j < 2 && k < 0; j++)
                if (PIC_JOB[j].gen != PIC_JOB[j].tried)
                    k = j;
            if (k < 0)
                pthread_cond_wait(&PIC_WAKE, &PIC_LOCK);
        }
        memcpy(w, PIC_JOB[k].w, sizeof w);
        memcpy(path, PIC_JOB[k].path, sizeof path);
        gen = PIC_JOB[k].gen;
        pthread_mutex_unlock(&PIC_LOCK);
        pic_draw((const int16_t (*)[CR_WT_N])w, COL[k], rgb, &p);
        j = png_write(path, rgb, PIC_W, PIC_H * PIC_FRAMES);
        pthread_mutex_lock(&PIC_LOCK);
        PIC_JOB[k].tried = gen;
        if (j)
            PIC_JOB[k].ok = gen;
        pthread_mutex_unlock(&PIC_LOCK);
    }
    return 0;
}
/* draw wave k's file in 3D into the skin (dir: the plugin's data folder); returns the request's number */
static uint32_t pic_request(const char *dir, int k, const int16_t (*w)[CR_WT_N])
{
    uint32_t g;
    pthread_mutex_lock(&PIC_LOCK);
    if (!PIC_RUNNING) {
        pthread_t t;
        if (pthread_create(&t, 0, pic_thread, 0) == 0) {
            pthread_detach(t);
            PIC_RUNNING = 1;
        }
    }
    memcpy(PIC_JOB[k].w, w, sizeof PIC_JOB[k].w);
    snprintf(PIC_JOB[k].path, sizeof PIC_JOB[k].path, "%s/../Plugin Skins/sh_crafty_file%d.png", dir, k + 1);
    g = ++PIC_JOB[k].gen;
    pthread_cond_signal(&PIC_WAKE);
    pthread_mutex_unlock(&PIC_LOCK);
    return g;
}
static int pic_done(int k, uint32_t g)          /* request g for wave k is written */
{
    int d;
    pthread_mutex_lock(&PIC_LOCK);
    d = PIC_JOB[k].ok == g && g != 0u;
    pthread_mutex_unlock(&PIC_LOCK);
    return d;
}
