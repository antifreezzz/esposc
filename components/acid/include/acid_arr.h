#pragma once
#include <stdint.h>

#include "acid_seq.h"

enum { ACID_PAT_A, ACID_PAT_B, ACID_PAT_C, ACID_PAT_F, ACID_PAT_COUNT };

enum {
    ACID_DR_KICK = 1,
    ACID_DR_HAT = 2,
    ACID_DR_OHAT = 4,
    ACID_DR_CLAP = 8,
};

#define ACID_SECTION_BARS 4
#define ACID_SECTIONS 16
#define ACID_TRACK_BARS (ACID_SECTION_BARS * ACID_SECTIONS)

// Одна секция трека (4 такта). Автоматизация идёт от *_from к *_to за секцию.
typedef struct {
    uint8_t pattern;
    uint8_t fill_pattern; // играет в 4-м такте, если != pattern
    int8_t transpose;     // полутоны
    uint8_t wave;         // 0 пила, 1 меандр
    uint8_t drums;        // ACID_DR_*
    float cut_from, cut_to; // база среза, Гц (по экспоненте)
    float res_from, res_to;
    float env_scale;        // множитель глубины огибающей фильтра
} acid_section_t;

extern const acid_step_t *const acid_patterns[ACID_PAT_COUNT]; // по 16 шагов
extern const acid_section_t acid_track[ACID_SECTIONS];

const acid_section_t *acid_arr_section(int bar);
// Паттерн для такта с учётом филла.
int acid_arr_pattern(int bar);
// Положение внутри секции, [0, 1).
float acid_arr_progress(int bar, int step);

// Копия src с 2-3 перевёрнутыми флагами ACCENT/SLIDE на нотах. Ноты и паузы
// не меняются, акцентов остаётся от 2 до 6. Детерминирована по seed.
void acid_mutate(acid_step_t *dst, const acid_step_t *src, int len, uint32_t seed);
