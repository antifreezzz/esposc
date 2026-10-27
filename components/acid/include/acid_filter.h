#pragma once

// 4-полюсный ladder ФНЧ (TPT / zero-delay feedback) с мягким насыщением.
typedef struct {
    float s[4];
} acid_ladder_t;

void acid_ladder_reset(acid_ladder_t *f);

// res в [0, 1], около 0.95+ самовозбуждается. cutoff ограничен [20, 0.45*sr].
float acid_ladder_process(acid_ladder_t *f, float in, float cutoff_hz, float res, float sr);
