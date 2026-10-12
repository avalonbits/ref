/*
 * Hundredths of a second between two clock readings, as zap's timing.h.
 *
 * CLOCKS_PER_SEC is 100 on the Agon and 1000000 on a PC, and both divide
 * by 100 exactly, so this needs no floating point -- on the eZ80 a single
 * %f would pull in the whole float formatter. Anything shorter than a
 * hundredth reads as zero, as it does in zap and acc.
 */
#ifndef MD2REF_TIMING_H
#define MD2REF_TIMING_H

#include <time.h>

static inline unsigned elapsed_cs(clock_t begin, clock_t end)
{
    return (unsigned)((end - begin) / (CLOCKS_PER_SEC / 100));
}

#endif
