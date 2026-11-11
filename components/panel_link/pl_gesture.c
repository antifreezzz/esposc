#include "pl_gesture.h"

#include <math.h>
#include <stdlib.h>

#define SECTORS 15
#define SECTOR_DEG (360.0f / SECTORS)

// Угол по часовой от 12 часов, 0..360; -1 у центра.
static float angle(int x, int y)
{
    float dx = (float)(x - PL_CENTER), dy = (float)(y - PL_CENTER);
    if (dx * dx + dy * dy < (float)(PL_DEAD_R * PL_DEAD_R)) {
        return -1.0f;
    }
    float a = atan2f(dx, -dy) * (180.0f / (float)M_PI);
    return a < 0.0f ? a + 360.0f : a;
}

int pl_sector(int x, int y)
{
    float a = angle(x, y);
    if (a < 0.0f) {
        return -1;
    }
    return (int)((a + SECTOR_DEG / 2) / SECTOR_DEG) % SECTORS;
}

#define GRID_X 9                                // левый край первой карточки
#define GRID_Y (PL_IA_TOP_H + PL_IA_TABS_H + 4) // верх первого ряда
#define GRID_DX 156

int pl_grid_cell(int x, int y)
{
    int x0 = GRID_X - 3, y0 = GRID_Y - 2; // зазор поровну по краям
    if (x < x0 || x >= x0 + 3 * GRID_DX || y < y0 || y >= y0 + 5 * PL_IA_ROW_H) {
        return -1;
    }
    return ((y - y0) / PL_IA_ROW_H) * 3 + (x - x0) / GRID_DX;
}

void pl_grid_rect(int i, int *x, int *y)
{
    *x = GRID_X + (i % 3) * GRID_DX;
    *y = GRID_Y + (i / 3) * PL_IA_ROW_H;
}

void pl_turn_begin(pl_turn_t *t, int x, int y, int v, int lo, int hi)
{
    float a = angle(x, y);
    t->last_deg = a;
    t->lost = a < 0.0f;
    t->acc = 0.0f;
    t->v = v;
    t->lo = lo;
    t->hi = hi;
    t->skip = -1;
}

void pl_turn_skip(pl_turn_t *t, int v)
{
    t->skip = v;
}

int pl_turn_move(pl_turn_t *t, int x, int y)
{
    float a = angle(x, y);
    if (a < 0.0f) {
        t->lost = 1;
        return t->v;
    }
    if (t->lost) {
        t->lost = 0;
        t->last_deg = a;
        return t->v;
    }
    float d = a - t->last_deg;
    d = d > 180.0f ? d - 360.0f : d < -180.0f ? d + 360.0f : d;
    t->last_deg = a;
    t->acc += d * (float)(t->hi - t->lo + 1) / 360.0f;
    while (t->acc >= 1.0f) {
        t->acc -= 1.0f;
        t->v += 1 + (t->v + 1 == t->skip);
        if (t->v > t->hi) {
            t->v = t->hi - (t->hi == t->skip);
            t->acc = 0.0f; // упор: обратный ход сразу ведёт назад
        }
    }
    while (t->acc <= -1.0f) {
        t->acc += 1.0f;
        t->v -= 1 + (t->v - 1 == t->skip);
        if (t->v < t->lo) {
            t->v = t->lo + (t->lo == t->skip);
            t->acc = 0.0f;
        }
    }
    return t->v;
}

enum { IDLE, PRESSED, SMEAR, HELD };

int pl_press_step(pl_press_t *p, int down, int x, int y, uint32_t ms)
{
    p->began = 0;
    if (!down) {
        int was = p->state;
        p->state = IDLE;
        return was == PRESSED ? PL_PRESS_TAP : was == HELD ? PL_PRESS_RELEASE : was == SMEAR ? PL_PRESS_END : PL_PRESS_NONE;
    }
    p->x1 = x;
    p->y1 = y;
    if (p->state == IDLE) {
        p->state = PRESSED;
        p->x0 = x;
        p->y0 = y;
        p->t0 = ms;
        p->began = 1;
        return PL_PRESS_NONE;
    }
    if (p->state == PRESSED) {
        if (abs(x - p->x0) > PL_SLOP_PX || abs(y - p->y0) > PL_SLOP_PX) {
            p->state = SMEAR;
        } else if (ms - p->t0 >= PL_HOLD_MS) {
            p->state = HELD;
            return PL_PRESS_HOLD;
        }
    }
    return PL_PRESS_NONE;
}

void pl_press_where(const pl_press_t *p, int *x, int *y)
{
    *x = p->x0;
    *y = p->y0;
}

void pl_press_last(const pl_press_t *p, int *x, int *y)
{
    *x = p->x1;
    *y = p->y1;
}

int pl_press_began(const pl_press_t *p)
{
    return p->began;
}

int pl_press_tap(int ev)
{
    return ev == PL_PRESS_TAP || ev == PL_PRESS_END || ev == PL_PRESS_RELEASE;
}
