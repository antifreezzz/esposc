#pragma once

// Электронная бочка: синус с падающей высотой и экспоненциальным затуханием.
typedef struct {
    float phase;    // [0, 1)
    float pitch_env;
    float amp_env;
    float pitch_coef;
    float amp_coef;
    float sr;
} acid_kick_t;

void acid_kick_init(acid_kick_t *k, float sr);
void acid_kick_trigger(acid_kick_t *k);
float acid_kick_next(acid_kick_t *k);
