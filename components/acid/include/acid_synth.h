#pragma once
#include <stddef.h>

#include "acid_env.h"
#include "acid_filter.h"
#include "acid_kick.h"
#include "acid_osc.h"
#include "acid_seq.h"

// Монофонический acid-голос с секвенсором. Поля публичные ради тестов,
// снаружи трогать только res, wave и kick_on.
typedef struct {
    float sr;
    acid_osc_t osc;
    acid_ladder_t flt;
    acid_decay_t fenv;
    acid_amp_t amp;
    acid_seq_t seq;
    float pitch_log2;  // текущая высота, log2(Гц)
    float target_log2;
    float glide_coef;
    int gliding;
    float accent_gain;
    float fenv_amount; // Гц на пике огибающей фильтра
    float res;
    int wave;          // 0 пила, 1 меандр
    float lfo_phase;   // [0, 1)
    float lfo_inc;
    float g;           // коэффициент фильтра с управляющей частоты
    int ctrl_left;     // сэмплов до следующего пересчёта
    acid_kick_t kick;
    int kick_on;       // прямая бочка на каждую долю
} acid_synth_t;

void acid_synth_init(acid_synth_t *s, float sr);
void acid_synth_set_pattern(acid_synth_t *s, const acid_step_t *steps, int len);
void acid_synth_render(acid_synth_t *s, float *out, size_t n);
float acid_synth_pitch_hz(const acid_synth_t *s);
