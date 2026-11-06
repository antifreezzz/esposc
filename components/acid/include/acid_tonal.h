#pragma once
#include <stdint.h>

#define ACID_TONAL_KNOBS 16 // по энкодерам, индекс = энкодер - 1, 0 (страница) не используется
#define ACID_TONAL_STEPS 8
#define ACID_TONAL_BAR 96   // тиков клока на такт 4/4

// Слой тональности: круг до 8 шагов сдвига нот по тактам клока, в полутонах или по ступеням лада.
typedef struct {
    uint8_t knob[ACID_TONAL_KNOBS]; // сырые значения ручек, 0xFF - не трогали (для blob)
    int len;       // длина шага в тактах, 0 - слой выключен
    int nsteps;    // шагов в круге 1..8
    int order;     // 0 вперёд, 1 туда-обратно, 2 случайно
    int target;    // 0 оба синта, 1 синт 1, 2 синт 2
    int held_mode; // звучащая нота на границе: 0 старая высота, 1 слайд, 2 прыжок
    int scale;     // 0 хроматика, 1..7 лады
    int tonic;     // 0..11
    int8_t raw[ACID_TONAL_STEPS]; // шаги, значение на экране - 64
    int cur_len;   // длина текущего шага в тактах, защёлкнута на последней границе
    int phase;     // тик внутри такта от Start, идёт и при выключенном слое: границы на тактовых чертах
    int bars;      // тактов текущего шага позади
    int armed;     // ждём первый тик после Start: это сильная доля такта 1, он не считается
    int step, dir;
    uint32_t rnd;
} acid_tonal_t;

void acid_tonal_init(acid_tonal_t *t);
void acid_tonal_set(acid_tonal_t *t, int enc, int v);   // enc - индекс энкодера с 0, вне списка игнор
void acid_tonal_start(acid_tonal_t *t);                 // шаг 1, счёт с нуля
int acid_tonal_tick(acid_tonal_t *t);                   // 1 - на этом тике граница шага
int acid_tonal_active(const acid_tonal_t *t);
int acid_tonal_step(const acid_tonal_t *t);             // 0..7
int acid_tonal_note(const acid_tonal_t *t, int note);       // нота со сдвигом текущего шага
int acid_tonal_drone_note(const acid_tonal_t *t, int note); // нота дрона: тоника + шаг (даже при len == 0)
// Ступень лада degree (0 - тоника) от ноты base, кругом по октавам; в хроматике ступень - полутон.
int acid_tonal_degree_note(const acid_tonal_t *t, int base, int degree);
// То же в явном ладу scale (0 хроматика, 1..7 лады, как SCALE страницы 9); вне - хроматика.
#define ACID_TONAL_SCALES 8
int acid_tonal_scale_note(int scale, int base, int degree);
int acid_tonal_moves(const acid_tonal_t *t, int slot);      // двигать ли синт slot
uint32_t acid_tonal_ticks_left(const acid_tonal_t *t);      // тиков до границы
