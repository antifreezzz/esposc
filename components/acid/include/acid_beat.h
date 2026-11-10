#pragma once
#include <stdint.h>

#define ACID_BEAT_TICKS 6  // тиков на шестнадцатую
#define ACID_BEAT_STEPS 16 // шагов в такте

// Счётчик шестнадцатых и тактов от MIDI clock (24 PPQN).
typedef struct {
    int stopped, tick, step;
    uint32_t bar;
} acid_beat_t;

void acid_beat_init(acid_beat_t *b);     // не остановлен: клок без Start тоже играет
void acid_beat_start(acid_beat_t *b);    // Start: следующий тик - шаг 0 такта 0
void acid_beat_continue(acid_beat_t *b); // играть дальше с текущего места
void acid_beat_stop(acid_beat_t *b);
// Тик клока. 1 - с этого тика начинается шаг b->step (0..15) такта b->bar.
int acid_beat_tick(acid_beat_t *b);
// Шаг Grids, начавшийся на последнем тике (звать после acid_beat_tick), или -1. grid 0 - шестнадцатые, нечётные
// позже на swing тиков (0..3); grid 1 - триольная сетка: 12 шагов в такте по 8 тиков, свинг не действует.
int acid_beat_grid_due(const acid_beat_t *b, int grid, int swing);
