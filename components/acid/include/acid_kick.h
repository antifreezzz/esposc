#pragma once

// Электронная бочка: синус с падающей высотой и экспоненциальным затуханием.
typedef struct {
    float phase;    // [0, 1)
    float pitch_env;
    float amp_env;
    float pitch_coef;
    float amp_coef;
    float low_hz;   // тон
    float sweep_hz; // глубина «щелчка»: старт на low + sweep
    float sr;
} acid_kick_t;

void acid_kick_init(acid_kick_t *k, float sr);
void acid_kick_trigger(acid_kick_t *k);
// Тон, глубина и время спада высоты, время спада громкости.
void acid_kick_set(acid_kick_t *k, float low_hz, float sweep_hz, float pitch_s, float amp_s);
float acid_kick_next(acid_kick_t *k);
