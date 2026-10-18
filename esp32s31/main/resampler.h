/*
 * Simple 16-bit mono resampler for the USB UAC <-> HFP SCO bridge.
 *
 * Downsampling uses a box average over `ratio` input samples (anti-alias
 * low-pass), upsampling uses linear interpolation.  Quality is sufficient for
 * narrow/wide-band voice; replace with a polyphase FIR later if needed.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t in_rate;
    uint32_t out_rate;
    uint32_t ratio;      /* in_rate / out_rate (down) or out_rate / in_rate (up) */
    int16_t  last;       /* previous input sample, for upsampling */
    bool     have_last;
} resampler_t;

void resampler_reset(resampler_t *r, uint32_t in_rate, uint32_t out_rate);

/* Returns the number of output samples produced (<= out_capacity). */
size_t resampler_downsample(resampler_t *r, const int16_t *in, size_t in_samples,
                            int16_t *out, size_t out_capacity);

size_t resampler_upsample(resampler_t *r, const int16_t *in, size_t in_samples,
                          int16_t *out, size_t out_capacity);
