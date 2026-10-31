#pragma once
#include <stdint.h>

#include "esp_err.h"

// Экран ST7701 480x480 (RGB565), тач GT911, подсветка, LVGL через esp_lvgl_port.
// Рисовать в LVGL - под lvgl_port_lock().
esp_err_t display_start(void);
// Касание: 1 - палец на экране, координаты в пикселях экрана.
int display_touch(int *x, int *y);
// Кадры LVGL с прошлого вызова, среднее и худшее время перерисовки.
void display_stats(int *frames, int *mean_us, int *max_us);
