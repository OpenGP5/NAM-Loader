/* NAM A2-Lite capture player, control-processor UI. The bank and the switching are in a2.c (nam.h).
 *
 *   turn PARA     browse the bank (the capture playing keeps playing); after 5 s still: back
 *   push PARA     load the highlighted capture (browsing), or start browsing
 *   foot switch   bypass
 *   volume knob   output level
 *
 * Screen (160 x 80, yellow on near-black, the platform's sans font):
 *   header  AMP / BYPASS / LOADING nn / BANK, slot nn / 12 on the right
 *   name    the capture's name, large; a name wider than the screen slides to its end and back
 *   footer  input meter and the volume; while switching, a progress bar
 * The slot playing is remembered across power-off (the platform's store). */
#include "gp5_firmware.h"
#include "gp5_font.h"
#include "fonts/sans_12.h"
#include "fonts/sans_16.h"
#include "fonts/sans_32.h"
#include "nam.h"

#define STORE_ID 0x42504D41u                         /* "AMPB" */
#define W GP5_SCREEN_W
#define HEAD_H 16
#define NAME_Y HEAD_H
#define NAME_H 44
#define FOOT_Y (NAME_Y + NAME_H)
#define FOOT_H (GP5_SCREEN_H - FOOT_Y)
#define ROW_H 21
#define BROWSE_TICKS 500u                            /* 5 s */
#define LOADING_MIN 30u                              /* show "loading" at least 0.3 s */
#define PAUSE_TICKS 150u                             /* scrolling name: 1.5 s at each end */
#define SCROLL_TICKS_PER_PX 5u                       /* 20 px a second */
#define MARGIN 6

typedef struct { uint8_t r, g, b; } rgb_t;
static const rgb_t BG = { 12, 11, 8 }, YELLOW = { 255, 199, 44 }, INK = { 244, 242, 236 }, DIM = { 112, 110, 104 },
                   GHOST = { 78, 76, 72 }, TRACK = { 40, 38, 34 }, ONYEL = { 28, 22, 4 };
static uint32_t rgb(rgb_t c) { return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }

/* ---- controls ---- */
static unsigned level, ticks, browsing, browse_until, loading_until, show_empty_until;
static int cursor;

static void save_slot(int s) { int32_t v = s; gp5_store_save(STORE_ID, &v, sizeof v); }
static void choose(int s)
{
    if (s == nam_current()) return;
    nam_select(s);
    save_slot(s);
    loading_until = ticks + LOADING_MIN;
}

void fw_init(void)
{
    int32_t v = -1;
    level = 0; ticks = 0; browsing = 0; nam_bypass = 0;
    if (gp5_store_load(STORE_ID, &v, sizeof v) != sizeof v) v = -1;
    nam_start((int)v);
    cursor = nam_current() < 0 ? 0 : nam_current();
}
void fw_foot(void) { nam_bypass ^= 1u; }
const char *fw_name(void) { return "nam"; }
int fw_store_changed(uint32_t id) { return id == STORE_ID; }   /* tone files: picked up by nam_poll within ~0.1 s */
void fw_para_push(void)
{
    if (!browsing) { browsing = 1; cursor = nam_current() < 0 ? 0 : nam_current(); browse_until = ticks + BROWSE_TICKS; return; }
    if (!nam_slot_ok(cursor)) { show_empty_until = ticks + 100u; browse_until = ticks + BROWSE_TICKS; return; }
    choose(cursor);
    browsing = 0;
}
void fw_para_turn(int detents)
{
    if (!browsing) { browsing = 1; cursor = nam_current() < 0 ? 0 : nam_current(); }
    else cursor = ((cursor + detents) % NAM_SLOTS + NAM_SLOTS) % NAM_SLOTS;
    browse_until = ticks + BROWSE_TICKS;
}
void fw_knob(unsigned knob)                          /* every ~10 ms: also the clock for this file */
{
    int got;
    level = knob;
    ticks++;
    if (browsing && (int)(ticks - browse_until) >= 0) browsing = 0;
    got = nam_poll();
    if (got >= 0) { save_slot(got); browsing = 0; loading_until = ticks + LOADING_MIN; }   /* an upload landed */
}
unsigned fw_level(void) { return level; }
void fw_dsp_ready(void) {}
int fw_take(uint32_t *id, uint32_t *value) { (void)id; (void)value; return 0; }
unsigned fw_setting_count(void) { return 0; }
unsigned fw_active(void) { return nam_bypass ? 0u : GP5_LED(32, 10, 0); }   /* yellow; dim red in bypass */

/* ---- drawing ---- */
static uint16_t band[W * NAME_H];
static unsigned bh;                                  /* rows of the band in use */

static void fill(rgb_t c) { unsigned i; uint16_t p = gp5_color(rgb(c)); for (i = 0; i < W * bh; i++) band[i] = p; }
static void box(int x, int y, int w, int h, rgb_t c)
{
    int i, j; uint16_t p = gp5_color(rgb(c));
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++)
        if (i >= 0 && i < W && j >= 0 && j < (int)bh) band[j * W + i] = p;
}
static int text(int x, int baseline, const gp5_font_t *f, const char *s, rgb_t c)
{
    return gp5_draw_text(band, W, bh, x, baseline - (int)f->base, f, s, rgb(c));
}
static void num2(char *p, int v) { p[0] = (char)('0' + v / 10 % 10); p[1] = (char)('0' + v % 10); }

static void draw_head(unsigned mode)                 /* mode: 0 play, 1 bypass, 2 loading, 3 browse */
{
    char r[10] = "00 / 12", l[12] = "LOADING 00";
    const gp5_font_t *f = &gp5_font_sans_12;
    bh = HEAD_H; fill(BG);
    if (mode == 3) {
        text(MARGIN, 12, f, "BANK", YELLOW);
        text(W - MARGIN - gp5_text_width(f, "push to load"), 12, f, "push to load", DIM);
    } else {
        int s = nam_current();
        if (mode == 2) { num2(l + 8, s + 1); text(MARGIN, 12, f, s < 0 ? "LOADING" : l, YELLOW); }
        else text(MARGIN, 12, f, mode == 1 ? "BYPASS" : "AMP", mode == 1 ? DIM : YELLOW);
        if (s < 0) text(W - MARGIN - gp5_text_width(f, "built-in"), 12, f, "built-in", DIM);
        else { num2(r, s + 1); num2(r + 5, NAM_SLOTS); text(W - MARGIN - gp5_text_width(f, r), 12, f, r, DIM); }
    }
    gp5_blit(0, 0, W, HEAD_H, band);
}

/* the name, large, at offset `off` pixels to the left (scrolling) */
static void draw_name(const char *s, int off, rgb_t c)
{
    bh = NAME_H; fill(BG);
    text(MARGIN - off, 34, &gp5_font_sans_32, s, c);
    box(0, 0, MARGIN, NAME_H, BG); box(W - MARGIN, 0, MARGIN, NAME_H, BG);   /* framed by the margins */
    gp5_blit(0, NAME_Y, W, NAME_H, band);
}

/* where a scrolling name is: pause, slide (eased), pause, slide back; 0 when it fits */
static int scroll_off(const char *s, unsigned t)
{
    int d = gp5_text_width(&gp5_font_sans_32, s) - (W - 2 * MARGIN);
    unsigned mv, per, p, x;
    if (d <= 0) return 0;
    mv = (unsigned)d * SCROLL_TICKS_PER_PX; per = 2u * (PAUSE_TICKS + mv); p = t % per;
    if (p < PAUSE_TICKS) return 0;
    if (p < PAUSE_TICKS + mv) x = p - PAUSE_TICKS;
    else if (p < 2u * PAUSE_TICKS + mv) return d;
    else x = mv - (p - 2u * PAUSE_TICKS - mv);
    {   /* smoothstep: 3x^2 - 2x^3, x = 0..1 in 1/4096 */
        uint32_t u = (uint32_t)((uint64_t)x * 4096u / mv), e = (u * u >> 12) * (3u * 4096u - 2u * u) >> 12;
        return (int)(((uint32_t)d * e + 2048u) >> 12);
    }
}

/* input meter (-48 .. 0 dBFS) and the volume; or a progress bar while switching */
static unsigned meter;                               /* 0..128, falls slowly */
static void draw_foot(unsigned mode)
{
    char v[8] = "VOL 100";
    unsigned pct = (level * 100u + 127u) / 255u, w, p = nam_take_peak(), l16 = 0, m;
    bh = FOOT_H; fill(BG);
    if (p) { unsigned b = 31u - (unsigned)__builtin_clz(p); l16 = b * 16u + (b >= 4 ? (p >> (b - 4)) & 15u : 0u); }
    m = l16 > 23u * 16u ? l16 - 23u * 16u : 0u;       /* log2 above 2^23 (-48 dB) in 1/16 */
    if (m > 128u) m = 128u;
    meter = m >= meter ? m : (meter > 3u ? meter - 3u : 0u);
    if (mode == 2) {
        w = (W - 2u * MARGIN) * nam_progress() / 255u;
        box(MARGIN, 8, W - 2 * MARGIN, 4, TRACK); box(MARGIN, 8, (int)w, 4, YELLOW);
    } else {
        int tx;
        v[4] = pct >= 100 ? '1' : ' '; v[5] = pct >= 10 ? (char)('0' + pct / 10 % 10) : ' '; v[6] = (char)('0' + pct % 10);
        tx = W - MARGIN - gp5_text_width(&gp5_font_sans_12, "VOL 100");
        text(tx, 14, &gp5_font_sans_12, v, DIM);
        w = (unsigned)(tx - 6 - MARGIN);
        box(MARGIN, 7, (int)w, 4, TRACK);
        box(MARGIN, 7, (int)(w * meter / 128u), 4, mode == 1 ? GHOST : YELLOW);
    }
    gp5_blit(0, FOOT_Y, W, FOOT_H, band);
}

/* the bank: three rows around the cursor, the highlighted one filled */
static void draw_rows(void)
{
    static uint16_t rows[W * (GP5_SCREEN_H - HEAD_H)];
    int k;
    for (k = 0; k < 3; k++) {
        int s = (cursor + k - 1 + NAM_SLOTS) % NAM_SLOTS, hl = k == 1;
        char n[4] = "00 ";
        const char *name = nam_slot_ok(s) ? nam_slot_name(s) : ((int)(ticks - show_empty_until) < 0 && hl ? "nothing to load" : "empty");
        bh = ROW_H; fill(hl ? YELLOW : BG);
        num2(n, s + 1);
        text(MARGIN, 16, &gp5_font_sans_16, n, hl ? ONYEL : DIM);
        text(MARGIN + 26, 16, &gp5_font_sans_16, name, hl ? ONYEL : (nam_slot_ok(s) ? INK : GHOST));
        box(W - MARGIN - 12, 0, MARGIN + 12, ROW_H, hl ? YELLOW : BG);           /* long names end before the dot */
        if (s == nam_current()) box(W - MARGIN - 5, 8, 5, 5, hl ? ONYEL : YELLOW);   /* the one playing */
        {   unsigned i; for (i = 0; i < W * ROW_H; i++) rows[(unsigned)k * W * ROW_H + i] = band[i]; }
    }
    for (k = 3 * W * ROW_H; k < W * (GP5_SCREEN_H - HEAD_H); k++) rows[k] = gp5_color(rgb(BG));   /* the row left at the bottom */
    gp5_blit(0, HEAD_H, W, GP5_SCREEN_H - HEAD_H, rows);
}

int fw_draw(unsigned knob, int force)
{
    static unsigned sh_mode = 99, sh_cur = 99, sh_cursor = 99, sh_emp, sh_foot, part;
    static int sh_off = -1;
    static unsigned name_t0;                         /* a new name starts its scrolling from the start */
    unsigned mode = browsing ? 3u : (nam_busy() || (int)(ticks - loading_until) < 0) ? 2u : nam_bypass ? 1u : 0u;
    unsigned cur = (unsigned)(nam_current() + 1), emp = (int)(ticks - show_empty_until) < 0;
    (void)knob;
    if (force || mode != sh_mode || cur != sh_cur) {
        if (cur != sh_cur || mode == 2) name_t0 = ticks;
        draw_head(mode); sh_mode = mode; sh_cur = cur;
        sh_off = -1; sh_cursor = 99;                 /* the rest follows on the next passes */
        return 1;
    }
    if (mode == 3) {
        if ((unsigned)cursor != sh_cursor || emp != sh_emp) { draw_rows(); sh_cursor = (unsigned)cursor; sh_emp = emp; }
        return 0;
    }
    if (part++ & 1u) {                               /* name and footer take turns */
        const char *n = nam_name();
        int off = scroll_off(n, ticks - name_t0);
        if (off != sh_off) { draw_name(n, off, mode == 1 ? GHOST : INK); sh_off = off; }
    } else if (ticks - sh_foot >= 3u) {              /* ~30 times a second */
        draw_foot(mode); sh_foot = ticks;
    }
    return 0;
}
