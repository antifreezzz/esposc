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

void pl_turn_begin(pl_turn_t *t, int x, int y, int v, int lo, int hi)
{
    float a = angle(x, y);
    t->last_deg = a;
    t->lost = a < 0.0f;
    t->acc = 0.0f;
    t->v = v;
    t->lo = lo;
    t->hi = hi;
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
        if (++t->v > t->hi) {
            t->v = t->hi;
            t->acc = 0.0f; // упор: обратный ход сразу ведёт назад
        }
    }
    while (t->acc <= -1.0f) {
        t->acc += 1.0f;
        if (--t->v < t->lo) {
            t->v = t->lo;
            t->acc = 0.0f;
        }
    }
    return t->v;
}

enum { IDLE, PRESSED, SMEAR, HELD };

int pl_press_step(pl_press_t *p, int down, int x, int y, uint32_t ms)
{
    if (!down) {
        int was = p->state;
        p->state = IDLE;
        return was == PRESSED ? PL_PRESS_TAP : was == HELD ? PL_PRESS_RELEASE : PL_PRESS_NONE;
    }
    if (p->state == IDLE) {
        p->state = PRESSED;
        p->x0 = x;
        p->y0 = y;
        p->t0 = ms;
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
