// Экран секвенсера панели: раскладка, уровни полос, жест рисования, ячейки песни.
#include <string.h>

#include "pl_labels.h"
#include "pl_proto.h"
#include "pl_seq_ui.h"
#include "test.h"

static void center(int view, int id, int idx, int row, int *cx, int *cy)
{
    int x, y, w, h;
    pl_seq_rect(view, id, idx, row, &x, &y, &w, &h);
    *cx = x + w / 2;
    *cy = y + h / 2;
}

TEST(test_bars_tile)
{
    for (int a = 0; a < 16; a++) {
        int x, y, w, h;
        CHECK(pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, a, 0, &x, &y, &w, &h));
        CHECK(w == 60 && h >= 150 && y >= 40 && y + h <= 480);
        for (int b = 0; b < 16; b++) {
            int x2, y2, w2, h2;
            pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, b, 0, &x2, &y2, &w2, &h2);
            if (a != b) {
                CHECK(x + w <= x2 || x2 + w2 <= x || y + h <= y2 || y2 + h2 <= y);
            }
            if (a < 8 && b >= 8) {
                CHECK(y + h <= y2); // шаги 1-8 сверху
            }
        }
    }
    CHECK(!pl_seq_rect(PL_SEQ_SONG, PL_ST_BAR, 0, 0, &(int){0}, &(int){0}, &(int){0}, &(int){0}));
}

TEST(test_level_edges)
{
    for (int s = 0; s < 16; s += 9) {
        int x, y, w, h;
        pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, s, 0, &x, &y, &w, &h);
        CHECK(pl_seq_level(s, y) == 15);
        CHECK(pl_seq_level(s, y - 30) == 15); // выше яруса - край
        CHECK(pl_seq_level(s, y + h - 1) == 0);
        int mid = pl_seq_level(s, y + h / 2);
        CHECK(mid >= 6 && mid <= 9);
        for (int yy = y; yy < y + h - 1; yy++) {
            CHECK(pl_seq_level(s, yy) >= pl_seq_level(s, yy + 1)); // вниз - не выше
        }
    }
}

TEST(test_drag_draws_line)
{
    uint8_t pat[16];
    memset(pat, PL_SEQ_REST, sizeof pat);
    pl_seq_drag_t d;
    pl_seq_drag_begin(&d, PL_SEQ_BRUSH_NOTE);
    int x, y, w, h;
    pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, 0, 0, &x, &y, &w, &h);
    int yy = y + 20;
    int lvl = pl_seq_level(0, yy);
    CHECK(lvl > 0);
    int changed = 0;
    for (int xx = 0; xx < 480; xx += 7) {
        changed += pl_seq_drag(&d, pat, xx, yy) >= 0;
    }
    for (int s = 0; s < 8; s++) {
        CHECK(pat[s] == lvl - 1);
    }
    for (int s = 8; s < 16; s++) {
        CHECK(pat[s] == PL_SEQ_REST);
    }
    CHECK(changed == 8); // повтор той же точки ничего не шлёт
}

TEST(test_drag_rest_keeps_flags)
{
    uint8_t pat[16];
    memset(pat, PL_SEQ_REST, sizeof pat);
    pat[9] = 4 | PL_SEQ_ACC;
    pl_seq_drag_t d;
    pl_seq_drag_begin(&d, PL_SEQ_BRUSH_NOTE);
    int x, y, w, h;
    pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, 9, 0, &x, &y, &w, &h);
    CHECK(pl_seq_drag(&d, pat, x + 30, y + h - 2) == 9);
    CHECK(pat[9] == (PL_SEQ_REST | PL_SEQ_ACC));
}

TEST(test_brush_toggles_once)
{
    uint8_t pat[16];
    memset(pat, 3, sizeof pat);
    pl_seq_drag_t d;
    pl_seq_drag_begin(&d, PL_SEQ_BRUSH_ACC);
    int x, y, w, h;
    pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, 2, 0, &x, &y, &w, &h);
    CHECK(pl_seq_drag(&d, pat, x + 10, y + 50) == 2);
    CHECK(pl_seq_drag(&d, pat, x + 40, y + 90) == -1);
    CHECK(pl_seq_drag(&d, pat, x + 70, y + 90) == 3); // соседняя
    CHECK(pl_seq_drag(&d, pat, x + 20, y + 90) == -1); // вернулись: уже переключён
    CHECK(pat[2] == (3 | PL_SEQ_ACC) && pat[3] == (3 | PL_SEQ_ACC) && pat[4] == 3);
    pl_seq_drag_begin(&d, PL_SEQ_BRUSH_SLIDE);
    CHECK(pl_seq_drag(&d, pat, x + 10, y + 50) == 2);
    CHECK(pat[2] == (3 | PL_SEQ_ACC | PL_SEQ_SLIDE));
    pl_seq_drag_begin(&d, PL_SEQ_BRUSH_ACC); // новый проход снимает
    pl_seq_drag(&d, pat, x + 10, y + 50);
    CHECK(pat[2] == (3 | PL_SEQ_SLIDE));
    pl_seq_drag_begin(&d, PL_SEQ_BRUSH_OCT); // октава вверх
    CHECK(pl_seq_drag(&d, pat, x + 10, y + 50) == 2);
    CHECK(pat[2] == (3 | PL_SEQ_SLIDE | PL_SEQ_UP));
}

TEST(test_drag_crosses_tiers)
{
    uint8_t pat[16];
    memset(pat, PL_SEQ_REST, sizeof pat);
    pl_seq_drag_t d;
    pl_seq_drag_begin(&d, PL_SEQ_BRUSH_NOTE);
    int x, y, w, h;
    pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, 13, 0, &x, &y, &w, &h);
    CHECK(pl_seq_drag(&d, pat, x + 5, y + 10) == 13);
    CHECK(pat[13] != PL_SEQ_REST);
}

TEST(test_song_cells)
{
    int cx, cy, arg = -1, row = -1;
    CHECK(pl_seq_rect(PL_SEQ_SONG, PL_ST_CELL, 5, 3, &(int){0}, &(int){0}, &(int){0}, &(int){0}));
    center(PL_SEQ_SONG, PL_ST_CELL, 5, 3, &cx, &cy);
    CHECK(pl_seq_at(PL_SEQ_SONG, cx, cy, &arg, &row) == PL_ST_CELL && row == 3 && arg == 5);
    center(PL_SEQ_SONG, PL_ST_REP, 0, 7, &cx, &cy);
    CHECK(pl_seq_at(PL_SEQ_SONG, cx, cy, &arg, &row) == PL_ST_REP && row == 7);
    for (int r = 0; r < 8; r++) {
        int x, y, w, h;
        pl_seq_rect(PL_SEQ_SONG, PL_ST_REP, 0, r, &x, &y, &w, &h);
        CHECK(y + h <= 480 && w >= 44 && h >= 40);
    }
    CHECK(pl_seq_at(PL_SEQ_PATTERN, cx, cy, &arg, &row) != PL_ST_REP);
}

TEST(test_toolbar)
{
    static const int IDS[] = {PL_ST_SYNTH, PL_ST_VIEW, PL_ST_BPM, PL_ST_OCT_DN, PL_ST_OCT_UP, PL_ST_SCALE, PL_ST_BRUSH};
    for (int view = 0; view < 2; view++) {
        for (size_t i = 0; i < sizeof IDS / sizeof IDS[0]; i++) {
            int x, y, w, h, arg, row;
            if (!pl_seq_rect(view, IDS[i], 0, 0, &x, &y, &w, &h)) {
                CHECK(view == PL_SEQ_SONG); // окно и кисть - только у такта
                continue;
            }
            CHECK(w >= 44 && h >= 40 && y >= 40);
            CHECK(pl_seq_at(view, x + w / 2, y + h / 2, &arg, &row) == IDS[i]);
        }
    }
    for (int p = 0; p < 8; p++) {
        int cx, cy, arg = -1, row;
        center(PL_SEQ_PATTERN, PL_ST_PAT, p, 0, &cx, &cy);
        CHECK(pl_seq_at(PL_SEQ_PATTERN, cx, cy, &arg, &row) == PL_ST_PAT && arg == p);
    }
    int cx, cy, arg, row;
    center(PL_SEQ_PATTERN, PL_ST_BAR, 10, 0, &cx, &cy);
    CHECK(pl_seq_at(PL_SEQ_PATTERN, cx, cy, &arg, &row) == PL_ST_BAR && arg == 10);
    CHECK(pl_seq_at(PL_SEQ_PATTERN, 240, 20, &arg, &row) == PL_ST_NONE); // шапка не наша
}

TEST(test_bpm_drag_horizontal)
{
    CHECK(pl_seq_bpm_drag(120, 200, 200) == 120);
    CHECK(pl_seq_bpm_drag(120, 200, 230) > 120);  // вправо - быстрее
    CHECK(pl_seq_bpm_drag(120, 200, 170) < 120);
    CHECK(pl_seq_bpm_drag(120, 200, 479) == 200); // до правого края - потолок
    CHECK(pl_seq_bpm_drag(120, 200, 0) == 60);
}

TEST(test_accent_cap_visible)
{
    for (int syn = 0; syn < 2; syn++) {
        uint32_t sc = pl_page_color(PL_PAGE_SYNTH1 + syn);
        uint32_t acc = pl_seq_cap_color(3 | PL_SEQ_ACC, sc), plain = pl_seq_cap_color(3, sc);
        CHECK(plain == sc);
        CHECK(acc != sc); // у синта 1 цвет синта совпадал с цветом акцента
    }
}

int main(void)
{
    RUN(test_bars_tile);
    RUN(test_level_edges);
    RUN(test_drag_draws_line);
    RUN(test_drag_rest_keeps_flags);
    RUN(test_brush_toggles_once);
    RUN(test_drag_crosses_tiers);
    RUN(test_song_cells);
    RUN(test_toolbar);
    RUN(test_bpm_drag_horizontal);
    RUN(test_accent_cap_visible);
    TEST_MAIN_END();
}
