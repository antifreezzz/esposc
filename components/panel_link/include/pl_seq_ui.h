#pragma once
#include <stdint.h>
// Экран секвенсера на панели: раскладка в координатах экрана 480 x 480 (шапка 40 сверху - не наша), попадание
// пальцем и жест рисования такта. Одна таблица и для отрисовки, и для касаний.
// Вид ТАКТ: тулбар, такты A-H, два яруса по 8 полос (шаги 1-8 и 9-16). Вид ПЕСНЯ: тулбар и 8 строк по 8 ячеек
// плюс ячейка повтора.

#define PL_SEQ_LEVELS 16 // 0 - пауза, 1..15 - ступень 0..14
enum { PL_SEQ_PATTERN, PL_SEQ_SONG }; // вид экрана
enum { PL_SEQ_BRUSH_NOTE, PL_SEQ_BRUSH_ACC, PL_SEQ_BRUSH_SLIDE, PL_SEQ_BRUSHES };
enum {
    PL_ST_NONE,
    PL_ST_SYNTH,  // S1/S2, тап
    PL_ST_VIEW,   // ТАКТ/ПЕСНЯ, тап
    PL_ST_BPM,    // темп, тянуть как ручку
    PL_ST_OCT_DN, // только ТАКТ
    PL_ST_OCT_UP,
    PL_ST_BRUSH,  // кисть по кругу
    PL_ST_PAT,    // такт A-H, arg - номер
    PL_ST_BAR,    // полоса, arg - шаг 0..15
    PL_ST_CELL,   // ячейка песни: row, arg - ячейка 0..7
    PL_ST_REP,    // повтор строки: row
    PL_ST_IDS
};
// Что под точкой: arg - такт, шаг или ячейка; row - строка песни.
int pl_seq_at(int view, int x, int y, int *arg, int *row);
// Прямоугольник элемента id вида view; idx - такт, шаг или ячейка, row - строка песни. 0 - у вида нет такого.
int pl_seq_rect(int view, int id, int idx, int row, int *x, int *y, int *w, int *h);
// Уровень 0..15 по y в ярусе шага: полоска внизу - пауза, выше - ступени; за ярусом - край.
int pl_seq_level(int step, int y);

typedef struct {
    int brush;
    uint16_t done; // ACC/SLIDE: шаги, у которых флаг уже переключён за этот проход
} pl_seq_drag_t;
void pl_seq_drag_begin(pl_seq_drag_t *d, int brush);
// Палец в (x, y): шаг под пальцем меняется в pat. Возвращает шаг 0..15 или -1 (ничего не поменялось).
// NOTE - ступень по высоте (уровень 0 = пауза), флаги остаются; ACC/SLIDE - флаг переключается один раз за проход.
// Ярус выбирается по y: палец, ушедший в соседний ярус, рисует в нём.
int pl_seq_drag(pl_seq_drag_t *d, uint8_t pat[16], int x, int y);
// Темп по ходу пальца вдоль кнопки BPM: вправо быстрее, 3 px на 1 BPM, 60..200.
int pl_seq_bpm_drag(int bpm0, int x0, int x);
// Цвет шапки полосы: у акцента - кремовый, иначе цвет синта (у синта 1 он совпадает с оранжевым акцентом).
uint32_t pl_seq_cap_color(uint8_t step, uint32_t synth_color);
