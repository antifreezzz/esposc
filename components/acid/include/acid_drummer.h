#pragma once
#include <stdint.h>

#include "pl_grids.h"

// Барабанщик: решения Grids на шаг -> ноты по конечностям (две ноги, две руки) и сбивки.
// Чистый слой без зависимостей от синта: зовётся с тика клока, не из рендера.

enum { DRM_ON = 1, DRM_X, DRM_Y, DRM_KICK, DRM_SNARE, DRM_HAT, DRM_CHAOS, DRM_HAT_MODE, DRM_TIME,
       DRM_FILL_EVERY, DRM_FILL_LEN, DRM_FILL_NOW, DRM_SRC, DRM_SWING, DRM_ROLL }; // индексы enc страницы Grids
#define DRM_SWING_MAX 3 // нечётная шестнадцатая позже на 0..3 тика клока (50, 58, 67, 75 %)
enum { DRM_HAT_CLOSED, DRM_HAT_OPEN, DRM_HAT_MIX };
enum { DRM_TIME_HAT, DRM_TIME_RIDE };

#define ACID_DRUMMER_MAX 4 // нот на шаг: 4 конечности
#define ACID_DRUMMER_ROLLS 2 // дробь ведут две руки: малый и рука времени

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
    uint32_t kick_next;     // шаг (такт * шагов + шаг), на котором бочка продолжит очередь; 0 - очереди нет
    int kick_run;           // ударов бочки подряд до этого шага: чёт - правая нога, нечет - левая
    struct {
        uint8_t note, vel, left, every, wait; // осталось повторов, период и тиков до ближайшего
    } roll[ACID_DRUMMER_ROLLS];
    const pl_grids_pat_t *user; // свои рисунки [PL_GRIDS_USER] или NULL; живут вне барабанщика
} acid_drummer_t;

void acid_drummer_init(acid_drummer_t *d); // умолчания из таблицы спеки
void acid_drummer_use_user(acid_drummer_t *d, const pl_grids_pat_t *user);
// Сетка источника: PL_GRIDS_16 - шаг 6 тиков, 16 в такте; PL_GRIDS_12T - 8 тиков, 12 в такте (доля - 3 шага).
int acid_drummer_grid(const acid_drummer_t *d);
// Ручка enc 1..14; вне списка - игнор (возвращает 0), принято - 1.
int acid_drummer_set(acid_drummer_t *d, int enc, int v);
void acid_drummer_reset(acid_drummer_t *d); // Start/Stop: сбивки и крэш сброшены
// Шаг step такта bar (0..15, в триольной сетке 0..11): ноты в out, вернуть их число (0, если выключен).
int acid_drummer_step(acid_drummer_t *d, int step, uint32_t bar, acid_hit_t out[ACID_DRUMMER_MAX]);
// Тик клока, на котором шаг не начался: повторы дроби (ручка ROLL) в out, вернуть их число.
int acid_drummer_tick(acid_drummer_t *d, acid_hit_t out[ACID_DRUMMER_ROLLS]);
