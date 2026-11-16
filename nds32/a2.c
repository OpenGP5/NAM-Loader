/* NAM A2-Lite on both processors, control-processor part: layers 0..A2_SPLIT-1 of the model on the
 * DSP's input, in the audio link's interrupt; the residual stream and this side's share of the output go back to
 * the DSP, which runs the rest (../split.h).
 *
 * Captures come from a bank of NAM_SLOTS slots in the data area (../split.h: slot i at i * NAM_SLOT_BYTES; written
 * from the computer with OpenGP5 Launchpad), or the built-in default when the bank is empty.
 * Switching: the output fades out (NAM_FADE_OUT blocks), the main loop loads the new capture (parse, bind, prime
 * this side's layers), the DSP gets its copy over the link, and the output fades in (NAM_FADE_IN blocks: this
 * also covers the DSP's layers starting from silence). A capture written into a slot while this runs is picked
 * up within a moment and played (the upload writes its header last, so a half-written one is never seen). */
#pragma GCC optimize("O2")
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "../kernel/a2r.c"
#include "../kernel/a2r_blob.c"
#include "../default_capture.inc"
#include "../split.h"
#include "gp5_firmware.h"
#include "nam.h"

#define NAM_FADE_OUT 8u          /* blocks (1.45 ms each) */
#define NAM_FADE_IN 40u
#define UNITY 65536

enum { ST_PLAY, ST_FADE, ST_LOAD, ST_SEND };

static a2r_state_t a2;
static int32_t a2_mem[32000];           /* histories of layers 0..A2_SPLIT-1 */
static a2r_capture_t cap, probe;
static const uint32_t *cap_file;        /* the capture file being played: a slot or the built-in one */
static volatile uint32_t st, gain;      /* gain: Q16, sent to the DSP with every block */
static volatile int want;               /* the slot to load next (-1: built-in) */
static int cur = -2;                    /* the slot playing (-1: built-in) */
static uint32_t woff;                   /* next piece of the file for the DSP */
static uint32_t slot_crc[NAM_SLOTS], slot_bad[NAM_SLOTS];
static uint8_t slot_ok[NAM_SLOTS];
static char slot_name[NAM_SLOTS][25];
static uint32_t in_peak;                /* largest input sample since the screen last asked */

volatile uint32_t nam_lstm_cycles, nam_bypass, nam_blocks, nam_lost, nam_dsp_cycles, nam_cycles, nam_worst;
volatile uint32_t nam_a2_worst, nam_bind, nam_slow;   /* nam_slow: blocks over 95 %; at 50 the model stops */

/* Slot i's capture file (platform 0.4: the file "tone-NN" of NAM_FIRM, written by OpenGP5 Launchpad), or, until
 * Launchpad has moved the bank into files, the old fixed place in the data area; max = the bytes it may span. */
#define NAM_FIRM 0x42504D41u                         /* "AMPB" */
static const uint8_t *slot_at(int i, unsigned *max)
{
    char name[8] = "tone-00";
    uint32_t len;
    const uint8_t *p;
    name[5] = (char)('0' + (i + 1) / 10); name[6] = (char)('0' + (i + 1) % 10);
    if ((p = gp5_file(NAM_FIRM, name, &len))) { *max = len; return p; }
    *max = NAM_SLOT_BYTES;
    return GP5_DATA + (uint32_t)i * NAM_SLOT_BYTES;   /* (a sector a file now uses fails the capture's own checks) */
}

/* main loop: load slot `want` (or the built-in one), bind + prime this side's layers */
static void load(void)
{
    int s = want;
    unsigned max = 0;
    const uint8_t *f = s >= 0 ? slot_at(s, &max) : 0;
    if (f && a2r_capture_parse(&cap, f, max) == 0) cap_file = (const uint32_t *)f;
    else { s = -1; a2r_capture_parse(&cap, a2r_default_capture, sizeof a2r_default_capture); cap_file = a2r_default_capture; }
    nam_bind = (uint32_t)-a2r_bind_range(&a2, &cap.m, 0, A2_SPLIT, a2_mem, sizeof a2_mem);   /* 0 = ok */
    if (!nam_bind) a2r_prime(&a2);                    /* NAM's pad_start for these layers */
    nam_slow = 0; nam_a2_worst = 0; nam_worst = 0; woff = 0;
    cur = want = s;
}

/* one slot's header: changed? -> check the whole file. Returns 1 when a new capture arrived there. */
static int scan_slot(int i)
{
    unsigned max;
    const uint8_t *f = slot_at(i, &max);
    uint32_t c = max >= 68 ? a2r_capture_crc_field(f) : 0;
    int fresh;
    if (!c) { slot_ok[i] = 0; return 0; }             /* empty, or being written (its header comes last) */
    if ((c == slot_crc[i] && slot_ok[i]) || c == slot_bad[i]) return 0;
    if (a2r_capture_parse(&probe, f, max)) { slot_bad[i] = c; slot_ok[i] = 0; return 0; }
    fresh = c != slot_crc[i];                         /* (the same capture back after its sector was rewritten: not new) */
    slot_crc[i] = c; slot_ok[i] = 1;
    for (int k = 0; k < 25; k++) slot_name[i][k] = probe.name[k];
    return fresh;
}

static void on_block(const int32_t *in, int32_t *out)
{
    uint32_t t0 = __nds32__mfsr(NDS32_SR_PFMC0), t, i, pk = in_peak;
    if (st == ST_LOAD || (uint32_t)in[0] != MAGIC_D || nam_bind) { nam_lost++; out[0] = 0; return; }
    for (i = 0; i < A2R_BLOCK; i++) {
        int32_t v = in[LW_U + i];
        uint32_t a = v < 0 ? (uint32_t)(-(v + 1)) : (uint32_t)v;
        if (a > pk) pk = a;
    }
    in_peak = pk;
    if (st == ST_SEND || (uint32_t)in[LW_DSPCRC] != cap.crc) {   /* the DSP does not have this capture yet */
        uint32_t total = cap.length / 4u, n = total - woff;
        if ((uint32_t)in[LW_DSPCRC] == cap.crc) { st = ST_PLAY; gain = 0; }
        else {
            if (n > LW_W_MAX) n = LW_W_MAX;
            out[1] = (int32_t)woff; out[2] = (int32_t)n; out[3] = (int32_t)total; out[4] = (int32_t)cap.crc;
            for (i = 0; i < n; i++) out[LW_W_DATA + i] = (int32_t)cap_file[woff + i];
            out[0] = (int32_t)MAGIC_W;
            woff = woff + n >= total ? 0 : woff + n;
            gain = 0;
            return;
        }
    }
    if (st == ST_FADE) {
        gain = gain > UNITY / NAM_FADE_OUT ? gain - UNITY / NAM_FADE_OUT : 0;
        if (!gain) st = ST_LOAD;                       /* this block still goes out, silent; the main loop loads */
    } else if (gain < UNITY) {
        gain = gain + UNITY / NAM_FADE_IN < UNITY ? gain + UNITY / NAM_FADE_IN : UNITY;
    }
    if (nam_slow >= 50u) { nam_lost++; out[0] = 0; return; }
    a2r_run(&a2, in + LW_U, 0, out + LW_X, out + LW_Y, 0);
    t = __nds32__mfsr(NDS32_SR_PFMC0) - t0;
    nam_lstm_cycles = t;
    if (t > 397000u) nam_slow++;                       /* too slow: keep USB and the screen alive, stop the model */
    if (nam_blocks > 100 && t > nam_a2_worst) nam_a2_worst = t;
    out[LW_RTAG] = in[LW_TAG];
    out[LW_FLAGS] = (int32_t)((nam_bypass & 1u) | 2u);
    out[LW_SEQ] = (int32_t)(nam_blocks + 1u);          /* the DSP counts repeats / skips */
    out[LW_CTLCYC] = (int32_t)t;
    out[LW_CAPCRC] = (int32_t)cap.crc;
    out[LW_GAIN] = (int32_t)gain;
    out[0] = (int32_t)MAGIC_N;
    nam_dsp_cycles = (uint32_t)in[LW_DSPCYC];
    nam_cycles = __nds32__mfsr(NDS32_SR_PFMC0) - t0;
    if (nam_blocks > 100 && nam_cycles > nam_worst) nam_worst = nam_cycles;
    nam_blocks++;
}

/* ---- for the screen and controls (main loop) ---- */
int nam_slot_ok(int i) { return i >= 0 && i < NAM_SLOTS && slot_ok[i]; }
const char *nam_slot_name(int i) { return nam_slot_ok(i) ? slot_name[i] : ""; }
int nam_current(void) { return cur; }
const char *nam_name(void) { return cap.name; }
int nam_busy(void) { return st != ST_PLAY; }
unsigned nam_progress(void)                           /* 0..255 through a switch */
{
    uint32_t total = cap.length / 4u;
    if (st == ST_FADE) return 32u - gain / 2048u;
    if (st == ST_LOAD) return 48u;
    if (st == ST_SEND) return total ? 64u + 160u * woff / total : 64u;
    return 255u;
}
unsigned nam_take_peak(void)                          /* 0..2^31 */
{
    unsigned p = in_peak;
    in_peak = 0;
    return p;
}
void nam_select(int slot)
{
    if (slot != -1 && !nam_slot_ok(slot)) return;
    want = slot;
    if (st == ST_PLAY) st = gain ? ST_FADE : ST_LOAD;
}

/* main loop (fw_knob, every 10 ms): a pending load; new captures in the bank */
int nam_poll(void)
{
    int got = -1;
    if (st == ST_LOAD) { load(); st = ST_SEND; }
    else if (st == ST_PLAY && want != cur) st = gain ? ST_FADE : ST_LOAD;   /* chosen during a switch */
    {   /* one slot per call (every 10 ms): finding a file reads 47 headers */
        static int next;
        if (scan_slot(next)) got = next;
        next = (next + 1) % NAM_SLOTS;
    }
    if (got >= 0) { nam_select(got); return got; }    /* just uploaded: play it */
    return -2;
}

/* start on `slot` when it holds a capture, else the first slot that does, else the built-in one */
void nam_start(int slot)
{
    int i;
    __nds32__mtsr(1u, NDS32_SR_PFM_CTL);              /* performance counter 0: count cycles */
    __nds32__isb();
    for (i = 0; i < NAM_SLOTS; i++) scan_slot(i);
    if (!nam_slot_ok(slot)) for (slot = -1, i = 0; i < NAM_SLOTS && slot < 0; i++) if (slot_ok[i]) slot = i;
    want = slot;
    load();
    st = ST_SEND; gain = 0;
    gp5_link_start(on_block);
}
