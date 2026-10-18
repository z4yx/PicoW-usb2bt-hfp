#include "resampler.h"

#include <string.h>

void resampler_reset(resampler_t *r, uint32_t in_rate, uint32_t out_rate)
{
    memset(r, 0, sizeof(*r));
    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->ratio = (in_rate >= out_rate) ? (in_rate / out_rate) : (out_rate / in_rate);
}

size_t resampler_downsample(resampler_t *r, const int16_t *in, size_t in_samples,
                            int16_t *out, size_t out_capacity)
{
    if (r->ratio == 0 || r->in_rate < r->out_rate) {
        return 0;
    }

    size_t produced = 0;
    size_t pos = 0;
    while (pos + r->ratio <= in_samples && produced < out_capacity) {
        int32_t acc = 0;
        for (uint32_t i = 0; i < r->ratio; i++) {
            acc += in[pos + i];
        }
        out[produced++] = (int16_t)(acc / (int32_t)r->ratio);
        pos += r->ratio;
    }
    return produced;
}

size_t resampler_upsample(resampler_t *r, const int16_t *in, size_t in_samples,
                          int16_t *out, size_t out_capacity)
{
    if (r->ratio == 0 || r->out_rate < r->in_rate) {
        return 0;
    }

    size_t produced = 0;
    for (size_t i = 0; i < in_samples; i++) {
        int16_t cur = in[i];
        if (!r->have_last) {
            r->last = cur;
            r->have_last = true;
        }
        for (uint32_t k = 1; k <= r->ratio; k++) {
            if (produced >= out_capacity) {
                return produced;
            }
            /* linear interpolation from last to current, excluding last */
            int32_t v = (int32_t)r->last +
                        ((int32_t)(cur - r->last) * (int32_t)k) / (int32_t)r->ratio;
            out[produced++] = (int16_t)v;
        }
        r->last = cur;
    }
    return produced;
}
