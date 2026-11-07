#pragma once
#include <stddef.h>
#include <stdint.h>

// float моно -> int16 стерео (L=R), с ограничением [-1, 1] и округлением.
void acid_pcm_to_s16_stereo(const float *in, int16_t *out, size_t frames);
// float L/R -> int16 стерео чередованием, ограничение и округление как выше.
void acid_pcm_lr_to_s16(const float *l, const float *r, int16_t *out, size_t frames);
// Громкость int16 в Q15 (32768 = 1.0, не больше), округление к ближайшему. in и out могут совпадать.
void acid_pcm_gain_s16(const int16_t *in, int16_t *out, size_t n, int32_t gain_q15);
