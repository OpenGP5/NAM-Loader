/* NAM A2-Lite on both processors: the control processor runs layers 0..A2_SPLIT-1, the DSP the
 * rest and the output. Shared by both sides. Kernel: kernel/a2r.c (32-bit residual stream), model: any A2
 * capture (A2-Lite part), loaded at run time (below).
 *
 * Link words (GP5_LINK_WORDS 264, set in dsp/firmware.h):
 *   DSP -> control processor: [0] MAGIC_D, [1..64] guitar input u(n) (Q31), [65] DSP cycles, [66] block tag n
 *   control processor -> DSP: [0] MAGIC_N, [1..192] the residual stream out of layer A2_SPLIT-1 (rows of 3),
 *                             [193..256] the control processor's share of the output (Q31), [257] the tag of the
 *                             input it ran, [258] flags: bit 0 bypass, bit 1 valid, [259] sequence number,
 *                             [260] its cycles for the block
 *                             [261] the checksum of the capture it runs, [262] output gain (Q16; the DSP
 *                             ramps to it over the block: fades when switching captures)
 * The DSP keeps the last 8 input blocks and picks the one the tag names (its layers mix the input in too).
 *
 * The capture (a2r_blob.h file) is in a slot of the bank in the control processor's data area (below; written
 * from the computer with OpenGP5 Launchpad), or the built-in default when the bank is empty. The DSP gets its own copy over the link:
 * while the checksum it reports ([67]) is not the control processor's, the control processor sends the file
 * instead of audio: [0] MAGIC_W, [1] word offset, [2] word count (<= 256), [3] words in all, [4] checksum,
 * [5..] the words. The DSP runs its layers only on blocks whose [261] matches the capture it holds. */
#ifndef NAM_A2SPLIT_H
#define NAM_A2SPLIT_H
#define A2_SPLIT   19            /* first layer on the DSP (layers 19..22 there: 4 of 23) */
#define MAGIC_D    0x4E414D31u   /* "NAM1" */
#define MAGIC_N    0x4E414D32u   /* "NAM2" */
#define MAGIC_W    0x4E414D57u   /* "NAMW": a piece of the capture file */
#define LW_U       1
#define LW_DSPCYC  65
#define LW_TAG     66
#define LW_DSPCRC  67            /* the capture the DSP holds (0 = none) */
#define LW_X       1
#define LW_Y       193
#define LW_RTAG    257
#define LW_FLAGS   258
#define LW_SEQ     259
#define LW_CTLCYC  260
#define LW_CAPCRC  261
#define LW_GAIN    262
#define LW_W_DATA  5
#define LW_W_MAX   256

/* The capture bank in the control processor's data area: NAM_SLOTS slots of NAM_SLOT_BYTES, slot i at
 * i * NAM_SLOT_BYTES; each holds one a2r_blob.h file (A2-Lite: 4,448 bytes) or nothing (erased). */
#define NAM_SLOTS      12
#define NAM_SLOT_BYTES 4608u
#endif
