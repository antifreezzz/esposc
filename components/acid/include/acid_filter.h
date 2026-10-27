#pragma once

// 4-полюсный ladder ФНЧ (TPT / zero-delay feedback) с мягким насыщением.
typedef struct {
    float s[4];
} acid_ladder_t;

void acid_ladder_reset(acid_ladder_t *f);

// Коэффициент g = tan(pi*fc/sr), cutoff ограничен [20, 0.45*sr]. Дорогой: считать
// на управляющей частоте, а не на каждом сэмпле.
float acid_ladder_coef(float cutoff_hz, float sr);

// Один сэмпл с готовым коэффициентом. res в [0, 1], около 0.95+ самовозбуждается.
float acid_ladder_run(acid_ladder_t *f, float in, float g, float res);

// То же с расчётом коэффициента на месте (удобно в тестах).
float acid_ladder_process(acid_ladder_t *f, float in, float cutoff_hz, float res, float sr);
