#pragma once
#include <stddef.h>
#include <stdint.h>

// float моно -> int16 стерео (L=R), с ограничением [-1, 1] и округлением.
void acid_pcm_to_s16_stereo(const float *in, int16_t *out, size_t frames);
// float L/R -> int16 стерео чередованием, ограничение и округление как выше.
void acid_pcm_lr_to_s16(const float *l, const float *r, int16_t *out, size_t frames);
