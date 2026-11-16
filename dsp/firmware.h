/* NAM A2-Lite on both processors, DSP part (see ../split.h for the split and the link words).
 * The DSP sends the guitar input to the control processor at the start of each block (GP5_LINK_EARLY), which runs
 * layers 0..A2_SPLIT-1 and sends back the residual stream and its share of the output; the DSP runs the remaining
 * layers on that, adds the shares, and plays the result. Latency: the control processor's answer to block n
 * arrives a few blocks later (the tag says which input block it belongs to).
 * Its layers start from silence (no prime): the first ~50 ms after start-up are not exact.
 * The capture comes from the control processor over the link (split.h); the DSP holds no model of its own.
 * gp5_link.py raw 307f0400 -> [model cycles last][worst][sequence errors][slow blocks | bind error << 16][dup tags]
 *                                [old tags][capture checksum held][capture pieces received][bad captures]
 *                  raw 317f0400 -> link check (blocks without the control processor's words, repeats, skips, ...) */
#define GP5_AUDIO_LINK
#ifndef GP5_LINK_EARLY   /* build.py passes it too */
#define GP5_LINK_EARLY   /* the exchange starts at the start of each block (platform/dsp/audio.h) */
#endif
#define GP5_LINK_WORDS 264u

#include "../split.h"
#include "../kernel/a2r.c"
#include "../kernel/a2r_blob.c"

static a2r_state_t a2 GP5_LARGE;
static a2r_capture_t cap GP5_LARGE;
static uint32_t cfile[2048] GP5_LARGE;           /* the capture file as received (8 KB at most) */
static uint32_t have_crc, rx_crc, rx_next, rx_total, rx_pieces, rx_bad;
static int32_t a2_mem[10240] GP5_LARGE;          /* histories of layers A2_SPLIT..22 (36,168 bytes for A2-Lite) */
static int32_t uhist[8][GP5_BLOCK] GP5_LARGE;    /* the last 8 input blocks, by tag */
static int32_t ymine[GP5_BLOCK];
static uint32_t blk, tag_last, bind_err;
static int32_t g_prev;                           /* output gain at the end of the last block, Q16 */
static uint32_t cyc, worst, seq_bad, seq_last, slow, d_tagdup, d_tagold;
static uint32_t d_notok, d_dup, d_skip, d_since, d_gi, d_gap[8];

static void fw_init(void)
{
    blk = tag_last = 0;
    cyc = worst = seq_bad = seq_last = slow = d_tagdup = d_tagold = 0;
    d_notok = d_dup = d_skip = d_since = d_gi = 0;
    g_prev = 0; bind_err = 0; have_crc = rx_crc = rx_next = rx_total = rx_pieces = rx_bad = 0;
}

/* a piece of the capture file from the control processor; when the last one is in: check it, bind the layers */
static void take_piece(const int32_t *li)
{
    uint32_t off = (uint32_t)li[1], n = (uint32_t)li[2], total = (uint32_t)li[3], crc = (uint32_t)li[4], i;
    if (n > LW_W_MAX || total > 2048u || off + n > total || crc == have_crc) return;   /* (pieces still on the way) */
    if (off == 0) { rx_crc = crc; rx_total = total; rx_next = 0; }
    if (crc != rx_crc || off != rx_next || total != rx_total) return;   /* missed one: wait for the next round */
    for (i = 0; i < n; i++) cfile[off + i] = (uint32_t)li[LW_W_DATA + i];
    rx_next += n; rx_pieces++;
    if (rx_next < rx_total) return;
    rx_crc = 0;
    if (a2r_capture_parse(&cap, cfile, rx_total * 4u) || cap.crc != crc) { rx_bad++; return; }
    bind_err = (uint32_t)-a2r_bind_range(&a2, &cap.m, A2_SPLIT, cap.m.nl, a2_mem, sizeof a2_mem);
    have_crc = bind_err ? 0 : crc;
    slow = 0; worst = 0;
}

/* The new input goes to the control processor at once, in the exchange starting now; kept here by its tag. */
static void fw_link_early(const int32_t *in, int32_t *lo)
{
    uint32_t i;
    blk++;
    for (i = 0; i < GP5_BLOCK; i++) { lo[LW_U + i] = in[i]; uhist[blk & 7u][i] = in[i]; }
    lo[LW_TAG] = (int32_t)blk;
    lo[LW_DSPCRC] = (int32_t)have_crc;
    lo[0] = (int32_t)MAGIC_D;
}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    const int32_t *li = gp5_link_in();
    int32_t *lo = gp5_link_out();
    int ok = (uint32_t)li[0] == MAGIC_N;
    if ((uint32_t)li[0] == MAGIC_W) take_piece(li);
    uint32_t flags = ok ? (uint32_t)li[LW_FLAGS] : 0, i, t0 = *(volatile uint32_t *)0xE0001004u;   /* cycle counter */
    int have = 0;
    {
        int bad = !ok;
        if (!ok) d_notok++;
        else {
            uint32_t q = (uint32_t)li[LW_SEQ];
            if (q != seq_last + 1u) { seq_bad++; bad = 1; if (q == seq_last) d_dup++; else d_skip++; }
            seq_last = q;
        }
        d_since++;
        if (bad) { d_gap[d_gi++ & 7u] = d_since; d_since = 0; }
    }
    if (ok && (flags & 2u) && have_crc && (uint32_t)li[LW_CAPCRC] == have_crc && slow < 50u) {
        uint32_t tag = (uint32_t)li[LW_RTAG];
        if (tag == tag_last) d_tagdup++;                       /* the same answer again: do not run it twice */
        else if (blk - tag >= 8u) d_tagold++;                  /* its input is gone already */
        else {
            a2r_run(&a2, uhist[tag & 7u], &li[LW_X], 0, ymine, 1);
            have = 1;
        }
        tag_last = tag;
    }
    cyc = *(volatile uint32_t *)0xE0001004u - t0;
    if (cyc > worst && blk > 100u) worst = cyc;
    if (cyc > 264000u) slow++;                                 /* over 95 %: at 50 the model stops (dry/silent) */
    lo[LW_DSPCYC] = (int32_t)cyc;
    {   /* the control processor's gain (fades when switching captures), ramped over the block */
        int32_t g1 = have ? li[LW_GAIN] : 0, g0 = g_prev;
        if (g1 < 0) g1 = 0;
        if (g1 > 65536) g1 = 65536;
        for (i = 0; i < GP5_BLOCK; i++) {
            int32_t v = 0;
            if (flags & 1u) v = in[i];
            else if (have) {
                int64_t s = (int64_t)ymine[i] + li[LW_Y + i];
                int32_t g = g0 + (int32_t)(((g1 - g0) * (int32_t)(i + 1)) / (int32_t)GP5_BLOCK);
                if (g < 65536) s = (s * g) >> 16;
                v = s > 2147483647LL ? 2147483647 : s < -2147483648LL ? (int32_t)0x80000000 : (int32_t)s;
            }
            out_left[i] = v; out_right[i] = v;
        }
        g_prev = (flags & 1u) ? g_prev : g1;
    }
}

static int fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n)
{
    (void)payload; (void)n;
    if (cmd == 0x7F30) {   /* [cmd][len 40][cycles last][worst][sequence errors][slow | bind error << 16][dup tags]
                              [old tags][capture checksum][pieces received][bad captures] */
        uint32_t v[9] = { cyc, worst, seq_bad, slow | (bind_err << 16), d_tagdup, d_tagold, have_crc, rx_pieces, rx_bad }, k;
        uint8_t r[40] = { 0x30, 0x7F, 40, 0 };
        for (k = 0; k < 36; k++) r[4 + k] = (uint8_t)(v[k / 4] >> (8 * (k % 4)));
        gp5_reply(r, sizeof r);
        return 1;
    }
    if (cmd == 0x7F31) {   /* link check: [cmd][len 48][not ok][repeats][skips][exchanges][busy][no sync][lag][last 5 gaps] */
        uint32_t v[12] = { d_notok, d_dup, d_skip, ST[21], ST[22], ST[23], link_lag }, k;
        uint8_t r[52] = { 0x31, 0x7F, 52, 0 };
        for (k = 0; k < 5; k++) v[7 + k] = d_gap[(d_gi - 1u - k) & 7u];
        for (k = 0; k < 48; k++) r[4 + k] = (uint8_t)(v[k / 4] >> (8 * (k % 4)));
        gp5_reply(r, sizeof r);
        return 1;
    }
    return 0;
}
