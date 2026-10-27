#include "acid_pcm.h"

#include <math.h>

void acid_pcm_to_s16_stereo(const float *in, int16_t *out, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        float x = in[i];
        x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
        int16_t v = (int16_t)lrintf(x * 32767.0f);
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
}
