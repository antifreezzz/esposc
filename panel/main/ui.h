#pragma once
#include <stdint.h>

#include "pl_model.h"

// Интерфейс панели на LVGL. Ввод - свой (pl_gesture), LVGL только рисует.
void ui_start(void);
// Раз в цикл: касание (down, x, y), модель от синта, время.
void ui_step(pl_model_t *m, int down, int x, int y, uint32_t ms);
