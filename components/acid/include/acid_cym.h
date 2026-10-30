#pragma once
#include <stdint.h>

#include "acid_bank.h"

// Тарелки из набора сэмплов: слои по velocity, повторы по кругу, хвост, строй, баланс.
#define ACID_CYM_VOICES 6 // звучащих одновременно
#define ACID_CYM_SLOTS 8  // плюс уходящие (забранные и перебитые)
#define ACID_CYM_LAYERS 8 // счётчики повторов на слои; слои выше делят последний

enum { ACID_CYM_CRASH, ACID_CYM_RIDE, ACID_CYM_BELL, ACID_CYM_N }; // ноты 49, 51, 53

typedef struct {
    const acid_bank_rr_t *rr; // NULL - голос свободен
    const uint8_t *p;         // следующий байт данных
    uint32_t pos;             // кадров декодировано
    acid_adpcm_t dec[2];
    float prev[2], cur[2]; // соседние кадры для интерполяции
    float phase;           // 0..1 между prev и cur
    float amp;             // громкость удара
    float env;             // хвост ручкой
    float fade, fade_step; // уход: fade от 1 к 0, шаг 0 - не уходит
    uint8_t which, layer, rr_index;
} acid_cym_voice_t;

typedef struct {
    acid_cym_voice_t v[ACID_CYM_SLOTS];
    uint8_t rr_next[ACID_CYM_N][ACID_CYM_LAYERS];
    float gain[ACID_CYM_N];
    float tail_coef[ACID_CYM_N]; // множитель хвоста на сэмпл, 1 - естественный
    float rate[ACID_CYM_N];      // скорость чтения
    float pan_l[ACID_CYM_N], pan_r[ACID_CYM_N];
    float sens;
    float sr;
} acid_cym_t;

void acid_cym_init(acid_cym_t *c, float sr);
// Ручки по числу на экране BSP. Для райда хвост, строй и панорама пишутся и в купол; для купола - только gain.
void acid_cym_set(acid_cym_t *c, int which, float gain, int tail_knob, int tune_knob, int pan_knob);
void acid_cym_set_sens(acid_cym_t *c, float sens); // 0 - все удары как самый сильный, 1 - полная динамика
// vel 0..1. b NULL или ноты нет в наборе - ничего.
void acid_cym_trigger(acid_cym_t *c, const acid_bank_t *b, int note, float vel);
void acid_cym_next(acid_cym_t *c, float *l, float *r);
int acid_cym_voices(const acid_cym_t *c);   // занятые слоты
int acid_cym_sounding(const acid_cym_t *c); // занятые и не уходящие
