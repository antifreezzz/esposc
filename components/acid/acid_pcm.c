#include "acid_pcm.h"

#include <math.h>

static int16_t s16(float x)
{
    x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    return (int16_t)lrintf(x * 32767.0f);
}

void acid_pcm_to_s16_stereo(const float *in, int16_t *out, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        out[2 * i] = out[2 * i + 1] = s16(in[i]);
    }
}

void acid_pcm_lr_to_s16(const float *l, const float *r, int16_t *out, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        out[2 * i] = s16(l[i]);
        out[2 * i + 1] = s16(r[i]);
    }
}
