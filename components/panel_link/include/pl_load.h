#pragma once
#include <stdint.h>

// Загрузка ядер синта для панели: раз в PL_LOAD_MS кадр PL_LOAD.
#define PL_LOAD_MS 500
#define PL_LOAD_BLOCK_US 2000 // бюджет блока ядра 1
#define PL_LOAD_CHUNK_US 666  // бюджет куска ядра 0

// Проценты бюджета за период (0..255, выше 100 - перегруз), underrun-ы за период, ступень защиты 0..2.
typedef struct {
    uint8_t c1_mean, c1_worst, c0_mean, c0_worst, underruns, guard;
} pl_load_t;

// Накопители периода: суммы и худшее в мкс.
typedef struct {
    uint32_t c1_sum_us, c1_worst_us, blocks, c0_sum_us, c0_worst_us, chunks, underruns;
} pl_load_acc_t;

void pl_load_pack(pl_load_t *o, const pl_load_acc_t *a, int guard);
