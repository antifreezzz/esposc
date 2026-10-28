#pragma once
#include <stdint.h>

#include "acid_noise.h"

// Стартовые значения звука ударки.
#define ACID_HAT_HZ 7000.0f
#define ACID_HAT_CLOSED_S 0.02f
#define ACID_HAT_OPEN_S 0.2f
#define ACID_CLAP_HZ 1200.0f
#define ACID_CLAP_TAIL_S 0.1f
#define ACID_SNARE_TUNE_HZ 180.0f
#define ACID_SNARE_SNAPPY 0.6f
#define ACID_SNARE_DECAY_S 0.08f

// Хэт: шум через ФВЧ ~7 кГц, закрытый короткий, открытый длинный.
typedef struct {
    uint32_t rng;
    acid_svf_t svf;
    float g, k;
    float env;
    float coef_closed, coef_open, coef;
    float sr;
} acid_hat_t;

void acid_hat_init(acid_hat_t *h, float sr);
void acid_hat_trigger(acid_hat_t *h, int open);
// Тон (срез ФВЧ), спад закрытого и открытого.
void acid_hat_set(acid_hat_t *h, float tone_hz, float closed_s, float open_s);
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
    float sr;
} acid_clap_t;

void acid_clap_init(acid_clap_t *c, float sr);
void acid_clap_trigger(acid_clap_t *c);
// Тон (центр полосы) и хвост.
void acid_clap_set(acid_clap_t *c, float tone_hz, float tail_s);
float acid_clap_next(acid_clap_t *c);

// Малый: синус-корпус с коротким падением высоты плюс шумовые пружины через ФВЧ.
typedef struct {
    uint32_t rng;
    acid_svf_t svf;
    float g, k;
    float phase, tune_hz, pitch_env, pitch_coef;
    float body_env, body_coef;
    float noise_env, noise_coef;
    float snappy;   // 0 - только корпус, 1 - в основном пружины
    float sr;
} acid_snare_t;

void acid_snare_init(acid_snare_t *s, float sr);
void acid_snare_trigger(acid_snare_t *s);
// Тон корпуса, доля пружин 0..1, спад (до 1/e).
void acid_snare_set(acid_snare_t *s, float tune_hz, float snappy, float decay_s);
float acid_snare_next(acid_snare_t *s);
