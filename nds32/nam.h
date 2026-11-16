/* The capture bank and switching (a2.c), for the controls and the screen (firmware.c, ui.c). Main loop only. */
#ifndef NAM_H
#define NAM_H
#include "../split.h"
extern volatile uint32_t nam_bypass, nam_blocks, nam_lost, nam_dsp_cycles, nam_cycles, nam_worst;
void nam_start(int slot);              /* start on this slot (or the first full one, or the built-in capture) */
int nam_poll(void);                    /* every 10 ms; returns the slot a new upload landed in (now playing), else -2 */
void nam_select(int slot);             /* switch to a slot (-1: the built-in capture) */
int nam_slot_ok(int i);                /* slot i holds a good capture */
const char *nam_slot_name(int i);
int nam_current(void);                 /* the slot playing, -1 = built-in */
const char *nam_name(void);            /* the capture playing */
int nam_busy(void);                    /* switching */
unsigned nam_progress(void);           /* 0..255 through a switch */
unsigned nam_take_peak(void);          /* the largest input sample since the last call, 0..2^31 */
#endif
