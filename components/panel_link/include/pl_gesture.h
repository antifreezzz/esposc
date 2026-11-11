#pragma once
#include <stdint.h>

#include "pl_ia.h"

// Жест панели 480x480: попадание в ручку и вращение пальцем вокруг центра экрана.

#define PL_CENTER 240
#define PL_DEAD_R 40   // ближе к центру угол дрожит: ни сектора, ни вращения
#define PL_HOLD_MS 50 // удержание до захвата ручки: от случайного касания; мазок отсекает PL_SLOP_PX
#define PL_SLOP_PX 12  // сдвиг пальца больше - мазок, не удержание

// Сектор под точкой: 0..14 (энкодеры 2..16) по часовой от 12 часов, 24° на сектор; -1 у центра.
int pl_sector(int x, int y);
// Сетка карточек 3x5 между вкладками и доком: карточка 150 x 66, шаг 156 по горизонтали и PL_IA_ROW_H по вертикали.
#define PL_CARD_W 150
#define PL_CARD_H 66
// Ячейка сетки под точкой экрана: 0..14 (15 ручек, энкодеры 2..16), -1 в шапке, вкладках, доке или за краем.
// Зазоры между карточками отданы соседям: палец не проваливается.
int pl_grid_cell(int x, int y);
void pl_grid_rect(int i, int *x, int *y); // левый верх карточки i 0..14 на экране

// Вращение пальцем: полный оборот = hi - lo + 1 шагов, упор в lo и hi без накопления.
typedef struct {
    float last_deg; // прошлый угол пальца
    float acc;      // накопленные доли шага
    int lost;       // палец у центра: опору возьмём заново
    int v, lo, hi;
    int skip; // значение, которое крутилка перескакивает; -1 - нет
} pl_turn_t;

void pl_turn_begin(pl_turn_t *t, int x, int y, int v, int lo, int hi);
int pl_turn_move(pl_turn_t *t, int x, int y); // новое значение
void pl_turn_skip(pl_turn_t *t, int v);        // после begin: значение v крутилка перескакивает

enum { PL_PRESS_NONE, PL_PRESS_TAP, PL_PRESS_HOLD, PL_PRESS_RELEASE, PL_PRESS_END };

typedef struct {
    int state; // 0 нет касания, 1 нажато, 2 мазок, 3 захват
    int x0, y0;
    int x1, y1; // последняя точка пальца
    uint32_t t0;
    int began; // этот опрос - первый с пальцем на экране
} pl_press_t; // начальное состояние - нули

// Каждый опрос тача: down 1 - палец на экране. TAP - отпущен быстро и без сдвига, HOLD - захват (один раз),
// RELEASE - отпущен после захвата, END - отпущен после мазка (палец поплыл до захвата: для плиток это тоже тап).
int pl_press_step(pl_press_t *p, int down, int x, int y, uint32_t ms);
// Касание отпущено как нажатие кнопки или плитки: TAP, END и RELEASE после захвата (палец дольше PL_HOLD_MS).
int pl_press_tap(int ev);
// Касание началось этим опросом (один раз на касание): по нему срабатывает навигация, не дожидаясь отпускания.
int pl_press_began(const pl_press_t *p);
// Где начиналось касание.
void pl_press_where(const pl_press_t *p, int *x, int *y);
// Где палец был последним перед отпусканием.
void pl_press_last(const pl_press_t *p, int *x, int *y);
