#pragma once

// Электронная бочка: синус с падающей высотой и экспоненциальным затуханием.
#define ACID_KICK_LOW_HZ 50.0f
#define ACID_KICK_SWEEP_HZ 100.0f // старт 150 Гц
#define ACID_KICK_PITCH_S 0.03f
#define ACID_KICK_AMP_S 0.08f

typedef struct {
    float phase;    // [0, 1)
    float pitch_env;
    float amp_env;
    float pitch_coef;
    float amp_coef;
    float low_hz;   // тон
    float sweep_hz; // глубина «щелчка»: старт на low + sweep
    float hit_low, hit_sweep; // тон и глубина этого удара: ручки плюс разброс
    unsigned rnd;             // свой ГПСЧ разброса (детерминизм демо и тестов)
    float sr, inv_sr;
} acid_kick_t;

void acid_kick_init(acid_kick_t *k, float sr);
void acid_kick_trigger(acid_kick_t *k); // ровно по ручкам, без разброса
// Удар с силой v 0..1: тихий - меньше щелчка (высота стартует ниже), плюс разброс от удара к удару: тон +-0.5 %,
// глубина +-5 %. Очередь бочек (бластбит) не звучит одним и тем же ударом. Всё на триггере, рендер тот же.
void acid_kick_trigger_vel(acid_kick_t *k, float v);
// Тон, глубина и время спада высоты, время спада громкости.
void acid_kick_set(acid_kick_t *k, float low_hz, float sweep_hz, float pitch_s, float amp_s);
float acid_kick_next(acid_kick_t *k);
