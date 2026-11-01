#include "pl_xy.h"

void pl_xy_init(pl_xy_t *p, int x0, int y0, int w, int h)
{
    *p = (pl_xy_t){.x0 = x0, .y0 = y0, .w = w, .h = h};
}

int pl_xy_inside(const pl_xy_t *p, int x, int y)
{
    return x >= p->x0 && x < p->x0 + p->w && y >= p->y0 && y < p->y0 + p->h;
}

static int span(const pl_xy_t *p, int axis)
{
    return (axis ? p->h : p->w) - 1 - 2 * PL_XY_EDGE;
}

int pl_xy_value(const pl_xy_t *p, int axis, int px)
{
    int d = axis ? p->y0 + p->h - 1 - PL_XY_EDGE - px : px - p->x0 - PL_XY_EDGE;
    int n = span(p, axis);
    int v = (d * 127 + n / 2) / n;
    if (d < 0) {
        v = 0; // деление к нулю: отрицательное не округлять
    }
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

void pl_xy_point(const pl_xy_t *p, int vx, int vy, int *x, int *y)
{
    *x = p->x0 + PL_XY_EDGE + (vx * span(p, 0) + 63) / 127;
    *y = p->y0 + p->h - 1 - PL_XY_EDGE - (vy * span(p, 1) + 63) / 127;
}

int pl_xy_touch(pl_xy_t *p, int x, int y, const int cur[2], uint32_t ms, int out[2])
{
    if (!p->active) {
        p->active = 1;
        for (int a = 0; a < 2; a++) {
            p->start[a] = cur[a];
            p->sent[a] = -1;
        }
    }
    p->want[0] = pl_xy_value(p, 0, x);
    p->want[1] = pl_xy_value(p, 1, y);
    int mask = 0;
    for (int a = 0; a < 2; a++) {
        if (p->start[a] < 0 || p->want[a] == p->sent[a]) {
            continue;
        }
        if (p->sent[a] >= 0 && ms - p->at[a] < PL_XY_EVERY_MS) {
            continue;
        }
        p->sent[a] = out[a] = p->want[a];
        p->at[a] = ms;
        mask |= 1 << a;
    }
    return mask;
}

int pl_xy_release(pl_xy_t *p, int out[2])
{
    if (!p->active) {
        return 0;
    }
    p->active = 0;
    int mask = 0;
    for (int a = 0; a < 2; a++) {
        if (p->start[a] < 0 || p->sent[a] < 0) {
            continue;
        }
        int v = p->hold ? p->want[a] : p->start[a];
        if (v != p->sent[a]) {
            out[a] = v;
            mask |= 1 << a;
        }
    }
    return mask;
}
