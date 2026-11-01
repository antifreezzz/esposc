#pragma once
#include <stdint.h>

// Жест панели 480x480: попадание в ручку и вращение пальцем вокруг центра экрана.

#define PL_CENTER 240
#define PL_DEAD_R 40   // ближе к центру угол дрожит: ни сектора, ни вращения
#define PL_HOLD_MS 50 // удержание до захвата ручки: от случайного касания; мазок отсекает PL_SLOP_PX
#define PL_SLOP_PX 12  // сдвиг пальца больше - мазок, не удержание

// Сектор под точкой: 0..14 (энкодеры 2..16) по часовой от 12 часов, 24° на сектор; -1 у центра.
int pl_sector(int x, int y);
// Ячейка сетки 3x5 под точкой экрана 480x480 (шапка 40 px): 0..14 (15 ручек, энкодеры 2..16), -1 в шапке или за краем.
int pl_grid_cell(int x, int y);

// Вращение пальцем: полный оборот = hi - lo + 1 шагов, упор в lo и hi без накопления.
typedef struct {
    float last_deg; // прошлый угол пальца
    float acc;      // накопленные доли шага
    int lost;       // палец у центра: опору возьмём заново
    int v, lo, hi;
} pl_turn_t;

void pl_turn_begin(pl_turn_t *t, int x, int y, int v, int lo, int hi);
int pl_turn_move(pl_turn_t *t, int x, int y); // новое значение

enum { PL_PRESS_NONE, PL_PRESS_TAP, PL_PRESS_HOLD, PL_PRESS_RELEASE, PL_PRESS_END };

typedef struct {
    int state; // 0 нет касания, 1 нажато, 2 мазок, 3 захват
    int x0, y0;
    int x1, y1; // последняя точка пальца
    uint32_t t0;
} pl_press_t; // начальное состояние - нули

// Каждый опрос тача: down 1 - палец на экране. TAP - отпущен быстро и без сдвига, HOLD - захват (один раз),
// RELEASE - отпущен после захвата, END - отпущен после мазка (палец поплыл до захвата: для плиток это тоже тап).
int pl_press_step(pl_press_t *p, int down, int x, int y, uint32_t ms);
// Касание отпущено как нажатие кнопки или плитки: TAP, END и RELEASE после захвата (палец дольше PL_HOLD_MS).
int pl_press_tap(int ev);
// Где начиналось касание.
void pl_press_where(const pl_press_t *p, int *x, int *y);
// Где палец был последним перед отпусканием.
void pl_press_last(const pl_press_t *p, int *x, int *y);
