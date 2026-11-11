#pragma once
// C-обёртка голоса Plaits (Mutable Instruments, MIT): модель, ручки, гейт.
#ifdef __cplusplus
extern "C" {
#endif

#define ACID_PLAITS_MODELS 24
#define ACID_PLAITS_BLOCK 12 // Plaits считает блоками по 12 сэмплов на 48 кГц

typedef struct {
    int model;        // 0..23, порядок движков Plaits 1.2
    float note;       // MIDI-нота, дробная (глайд)
    float harmonics, timbre, morph; // 0..1
    float decay;      // 0..1, хвост LPG после гейта и внутренняя огибающая
    float lpg_colour; // 0..1
    float fm_env, timbre_env, morph_env; // -1..1, сколько внутренняя огибающая двигает высоту, timbre, morph
    float level;      // 0..1, уровень гейта (0 - нота отпущена)
    int trigger;      // 1 пока нота держится; фронт 0 -> 1 запускает ноту
    int sustained;    // 1 в режиме дрона: непрерывно возбуждает и ударные/модальные модели Plaits
} acid_plaits_params_t;

typedef struct acid_plaits acid_plaits_t;

// Выделяет память (один раз при старте, не из рендера) и инициализирует голос.
acid_plaits_t *acid_plaits_create(void);
void acid_plaits_init(acid_plaits_t *p);
// n == ACID_PLAITS_BLOCK, выход примерно -1..1.
void acid_plaits_render(acid_plaits_t *p, const acid_plaits_params_t *pp, float *out, int n);


// Только для тестов: быстрые 1/x и 1/sqrt(x) (x > 0), которыми в Plaits заменены деление и корень.
float acid_plaits_test_rcp(float x);
float acid_plaits_test_rsqrt(float x);

#ifdef __cplusplus
}
#endif
