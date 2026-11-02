#pragma once
#include <stdint.h>

// Барабанщик: решения Grids на шаг -> ноты по конечностям (две ноги, две руки) и сбивки.
// Чистый слой без зависимостей от синта: зовётся с тика клока, не из рендера.

enum { DRM_ON = 1, DRM_X, DRM_Y, DRM_KICK, DRM_SNARE, DRM_HAT, DRM_CHAOS, DRM_HAT_MODE, DRM_TIME,
       DRM_FILL_EVERY, DRM_FILL_LEN, DRM_FILL_NOW }; // индексы enc страницы Grids
enum { DRM_HAT_CLOSED, DRM_HAT_OPEN, DRM_HAT_MIX };
enum { DRM_TIME_HAT, DRM_TIME_RIDE };

#define ACID_DRUMMER_MAX 4 // нот на шаг: 4 конечности

typedef struct {
    uint8_t note, vel;
} acid_hit_t;

typedef struct {
    uint8_t knob[16];       // ручки страницы Grids (сырые, как пришли)
    int fill_from, fill_to; // шаги сбивки в текущем такте [from, to), from == to - нет
    int fill_pending;       // «сейчас» нажата: начать с ближайшей доли
    int crash_next;         // сбивка кончилась: крэш на следующем шаге 0
    uint8_t perturb[3];
    uint32_t rnd;           // ГПСЧ (LCG), сид в init - детерминизм тестов
} acid_drummer_t;

void acid_drummer_init(acid_drummer_t *d); // умолчания из таблицы спеки
// Ручка enc 1..12; вне списка - игнор (возвращает 0), принято - 1.
int acid_drummer_set(acid_drummer_t *d, int enc, int v);
void acid_drummer_reset(acid_drummer_t *d); // Start/Stop: сбивки и крэш сброшены
// Шаг step 0..15 такта bar: ноты в out, вернуть их число (0, если выключен).
int acid_drummer_step(acid_drummer_t *d, int step, uint32_t bar, acid_hit_t out[ACID_DRUMMER_MAX]);
