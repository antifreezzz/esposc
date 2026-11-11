#pragma once
#include <stdint.h>

// Стенд отклика интерфейса (сборка с -DUI_BENCH=1), см. ui_bench.c.
void ui_bench_start(void);
// Раз в цикл вместо тача: подставляет касание кнопки дока и пишет в лог перерисовки после прошлого касания.
void ui_bench_touch(uint32_t ms, int *down, int *x, int *y);
