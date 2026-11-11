#include "acid_pcm.h"

// Округление к ближайшему, половинки к чётному - как lrintf, но без libm: на плате lrintf - программный вызов
// на каждый сэмпл (66 мкс на блок 2 мс). |y| < 32768: y + 0.5 во float точно, усечение даёт пол.
static inline int16_t s16(float x)
{
    x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    float y = x * 32767.0f;
    float a = y < 0.0f ? -y : y;
    int32_t i = (int32_t)(a + 0.5f);
    if ((float)i - a == 0.5f) {
        i &= ~1; // ровно половина: к чётному
    }
    return (int16_t)(y < 0.0f ? -i : i);
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

void acid_pcm_gain_s16(const int16_t *in, int16_t *out, size_t n, int32_t gain_q15)
{
    for (size_t i = 0; i < n; i++) {
        // Округление симметрично: тихий сигнал не сползает в минус.
        int32_t a = in[i] < 0 ? -in[i] : in[i];
        int32_t y = (a * gain_q15 + 16384) >> 15;
        out[i] = (int16_t)(in[i] < 0 ? -y : y);
    }
}
