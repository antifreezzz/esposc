#pragma once
#include <stdint.h>

// XY-пэд своей страницы: ось 0 (X, слева направо) - слот E2, ось 1 (Y, снизу вверх) - слот E3.
// Отправка не чаще PL_XY_EVERY_MS на ось и только при смене. HOLD выкл: при отпускании оси возвращаются
// к значениям до касания.

#define PL_XY_EDGE 16    // мёртвая рамка по краю поля, px: у края всегда 0 или 127
#define PL_XY_EVERY_MS 10

typedef struct {
    int x0, y0, w, h; // поле на экране
    int hold;         // 1 - оси остаются, где отпустил
    int active;       // палец на пэде
    int start[2];     // значения осей при касании, -1 - ось пуста
    int want[2];      // где палец
    int sent[2];      // последнее отправленное, -1 - не слали
    uint32_t at[2];   // когда отправлено
} pl_xy_t;

void pl_xy_init(pl_xy_t *p, int x0, int y0, int w, int h);
int pl_xy_inside(const pl_xy_t *p, int x, int y);
// Координата экрана px по оси -> 0..127.
int pl_xy_value(const pl_xy_t *p, int axis, int px);
// Где рисовать точку для значений осей.
void pl_xy_point(const pl_xy_t *p, int vx, int vy, int *x, int *y);
// Каждый опрос, пока палец на пэде. cur - живые значения слотов, -1 - слот пуст. Маска осей к отправке
// (бит 0 - X, бит 1 - Y), значения в out.
int pl_xy_touch(pl_xy_t *p, int x, int y, const int cur[2], uint32_t ms, int out[2]);
// Палец отпущен: маска осей к отправке, значения в out (HOLD выкл - стартовые, вкл - недосланные).
int pl_xy_release(pl_xy_t *p, int out[2]);
