#pragma once
#include <stdint.h>

#include "acid_noise.h"

// Хэт: шум через ФВЧ ~7 кГц, закрытый короткий, открытый длинный.
typedef struct {
    uint32_t rng;
    acid_svf_t svf;
    float g, k;
    float env;
    float coef_closed, coef_open, coef;
} acid_hat_t;

void acid_hat_init(acid_hat_t *h, float sr);
void acid_hat_trigger(acid_hat_t *h, int open);
float acid_hat_next(acid_hat_t *h);

// Клэп: шум через полосовой ~1.2 кГц, 3 всплеска с шагом 10 мс и хвост.
typedef struct {
    uint32_t rng;
    acid_svf_t svf;
    float g, k;
    float env;
    float burst_coef, tail_coef;
    int t;          // сэмплов с триггера, -1 когда молчит
    int burst_step; // 10 мс в сэмплах
} acid_clap_t;

void acid_clap_init(acid_clap_t *c, float sr);
void acid_clap_trigger(acid_clap_t *c);
float acid_clap_next(acid_clap_t *c);
