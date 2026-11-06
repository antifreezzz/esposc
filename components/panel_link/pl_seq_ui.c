#include "pl_seq_ui.h"

#include "pl_proto.h"

#define TOOL_Y 44 // тулбар
#define TOOL_H 44
#define PAT_Y 92 // такты A-H (вид ТАКТ)
#define PAT_H 40
#define PAT_W 59
#define TIER_Y 138 // ярус шагов 1-8, следом 9-16
#define TIER_H 168
#define TIER_GAP 6
#define BAR_W 60
#define REST_H 18 // полоска паузы внизу яруса
#define LEVEL_H 10 // (TIER_H - REST_H) / 15
#define SONG_Y 96 // строки песни
#define ROW_H 48
#define CELL_W 50
#define REP_X 408

typedef struct {
    short id, x0, x1;
    char pattern_only;
} tool_t;

static const tool_t TOOLS[] = {
    {PL_ST_SYNTH, 4, 84, 0},    {PL_ST_VIEW, 88, 188, 0},     {PL_ST_BPM, 192, 280, 0},
    {PL_ST_OCT_DN, 284, 334, 1}, {PL_ST_OCT_UP, 338, 388, 1}, {PL_ST_BRUSH, 392, 476, 1},
};
#define NTOOLS (int)(sizeof TOOLS / sizeof TOOLS[0])

static int set(int *x, int *y, int *w, int *h, int x0, int y0, int w0, int h0)
{
    *x = x0, *y = y0, *w = w0, *h = h0;
    return 1;
}

static int tier_y(int tier)
{
    return TIER_Y + tier * (TIER_H + TIER_GAP);
}

int pl_seq_rect(int view, int id, int idx, int row, int *x, int *y, int *w, int *h)
{
    *x = *y = *w = *h = 0;
    for (int i = 0; i < NTOOLS; i++) {
        if (TOOLS[i].id == id) {
            if (TOOLS[i].pattern_only && view != PL_SEQ_PATTERN) {
                return 0;
            }
            return set(x, y, w, h, TOOLS[i].x0, TOOL_Y, TOOLS[i].x1 - TOOLS[i].x0, TOOL_H);
        }
    }
    if (view == PL_SEQ_PATTERN) {
        if (id == PL_ST_PAT && idx >= 0 && idx < PL_SEQ_PATS) {
            return set(x, y, w, h, 4 + idx * PAT_W, PAT_Y, PAT_W - 4, PAT_H);
        }
        if (id == PL_ST_BAR && idx >= 0 && idx < PL_SEQ_STEPS) {
            return set(x, y, w, h, (idx % 8) * BAR_W, tier_y(idx / 8), BAR_W, TIER_H);
        }
        return 0;
    }
    if (row < 0 || row >= PL_SEQ_ROWS) {
        return 0;
    }
    if (id == PL_ST_CELL && idx >= 0 && idx < PL_SEQ_CELLS) {
        return set(x, y, w, h, 4 + idx * CELL_W, SONG_Y + row * ROW_H, CELL_W - 4, ROW_H - 4);
    }
    if (id == PL_ST_REP) {
        return set(x, y, w, h, REP_X, SONG_Y + row * ROW_H, 476 - REP_X, ROW_H - 4);
    }
    return 0;
}

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

int pl_seq_at(int view, int px, int py, int *arg, int *row)
{
    int x, y, w, h;
    *arg = *row = 0;
    for (int i = 0; i < NTOOLS; i++) {
        if (pl_seq_rect(view, TOOLS[i].id, 0, 0, &x, &y, &w, &h) && inside(px, py, x, y, w, h)) {
            return TOOLS[i].id;
        }
    }
    if (view == PL_SEQ_PATTERN) {
        for (int p = 0; p < PL_SEQ_PATS; p++) {
            if (pl_seq_rect(view, PL_ST_PAT, p, 0, &x, &y, &w, &h) && inside(px, py, x, y, w, h)) {
                *arg = p;
                return PL_ST_PAT;
            }
        }
        for (int s = 0; s < PL_SEQ_STEPS; s++) {
            if (pl_seq_rect(view, PL_ST_BAR, s, 0, &x, &y, &w, &h) && inside(px, py, x, y, w, h)) {
                *arg = s;
                return PL_ST_BAR;
            }
        }
        return PL_ST_NONE;
    }
    for (int r = 0; r < PL_SEQ_ROWS; r++) {
        for (int c = 0; c < PL_SEQ_CELLS; c++) {
            if (pl_seq_rect(view, PL_ST_CELL, c, r, &x, &y, &w, &h) && inside(px, py, x, y, w, h)) {
                *arg = c, *row = r;
                return PL_ST_CELL;
            }
        }
        if (pl_seq_rect(view, PL_ST_REP, 0, r, &x, &y, &w, &h) && inside(px, py, x, y, w, h)) {
            *row = r;
            return PL_ST_REP;
        }
    }
    return PL_ST_NONE;
}

int pl_seq_level(int step, int y)
{
    int dy = y - tier_y(step / 8);
    if (dy >= TIER_H - REST_H) {
        return 0;
    }
    int lvl = PL_SEQ_LEVELS - 1 - (dy < 0 ? 0 : dy / LEVEL_H);
    return lvl < 1 ? 1 : lvl;
}

void pl_seq_drag_begin(pl_seq_drag_t *d, int brush)
{
    d->brush = brush;
    d->done = 0;
}

int pl_seq_drag(pl_seq_drag_t *d, uint8_t pat[16], int x, int y)
{
    int tier = y < tier_y(1) - TIER_GAP / 2 ? 0 : 1;
    int col = x < 0 ? 0 : (x >= 8 * BAR_W ? 7 : x / BAR_W);
    int step = tier * 8 + col;
    uint8_t old = pat[step], v = old;
    if (d->brush == PL_SEQ_BRUSH_NOTE) {
        int lvl = pl_seq_level(step, y);
        v = (uint8_t)((old & ~PL_SEQ_DEG) | (lvl == 0 ? PL_SEQ_REST : lvl - 1));
    } else if (!(d->done >> step & 1)) {
        d->done |= (uint16_t)(1u << step);
        v = old ^ (d->brush == PL_SEQ_BRUSH_ACC ? PL_SEQ_ACC : PL_SEQ_SLIDE);
    }
    if (v == old) {
        return -1;
    }
    pat[step] = v;
    return step;
}

#define BPM_PX 3
#define CAP_ACC 0xf1e7d0 // C_TEXT панели

int pl_seq_bpm_drag(int bpm0, int x0, int x)
{
    int bpm = bpm0 + (x - x0) / BPM_PX;
    return bpm < 60 ? 60 : bpm > 200 ? 200 : bpm;
}

uint32_t pl_seq_cap_color(uint8_t step, uint32_t synth_color)
{
    return step & PL_SEQ_ACC ? CAP_ACC : synth_color;
}
