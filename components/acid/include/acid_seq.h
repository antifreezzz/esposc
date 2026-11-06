#pragma once
#include <stddef.h>
#include <stdint.h>

#include "acid_tonal.h"

// Встроенный секвенсер синтов 1 и 2: 8 тактов по 16 шагов и песня из строк тактов с повтором.
// Живёт вне acid_synth_t (переживает смену режима), синт зовёт его на тике клока в фазе событий.

#define ACID_SEQ_SYNTHS 2
#define ACID_SEQ_PATS 8
#define ACID_SEQ_STEPS 16
#define ACID_SEQ_ROWS 8
#define ACID_SEQ_CELLS 8
#define ACID_SEQ_REST 0x0F  // ступень паузы
#define ACID_SEQ_DEG 0x0F   // маска ступени 0..14
#define ACID_SEQ_ACC 0x10
#define ACID_SEQ_SLIDE 0x20
#define ACID_SEQ_UP 0x40    // нота на октаву выше
#define ACID_SEQ_EMPTY 0xFF // пустая ячейка песни
#define ACID_SEQ_BPM_MIN 60
#define ACID_SEQ_BPM_MAX 200
#define ACID_SEQ_REP_MAX 16
#define ACID_SEQ_OCT_MAX 2
#define ACID_SEQ_SCALES 9 // лад такта: 0 - со страницы 9 TONAL, 1 + k - лад k (0 хроматика ... 7 пентатоника)

typedef struct {
    uint8_t pat[ACID_SEQ_PATS][ACID_SEQ_STEPS];
    int8_t oct[ACID_SEQ_PATS];
    uint8_t scale[ACID_SEQ_PATS];
    uint8_t cell[ACID_SEQ_ROWS][ACID_SEQ_CELLS], rep[ACID_SEQ_ROWS];
    uint8_t run, edit; // edit - такт на экране, играет при пустой песне
    // Позиция: строка, проход строки (0..rep-1), ячейка, шаг, тик в шаге, играющий такт.
    uint8_t row, n, c, step, tick, cur_pat;
    int8_t sounding; // звучащая MIDI-нота, -1 нет
    uint8_t armed;   // ждём шаг 0 общего такта
    uint8_t slid;    // звучащая тянется слайдом в следующий шаг
} acid_seq_track_t;

typedef struct {
    acid_seq_track_t t[ACID_SEQ_SYNTHS];
    uint8_t bpm;
} acid_seq_t;

typedef struct {
    uint8_t synth, on, note, vel;
} acid_seq_ev_t;

void acid_seq_init(acid_seq_t *q); // всё пусто (пауза), rep = 1, ячейки EMPTY, bpm 120, run 0
void acid_seq_set_step(acid_seq_t *q, int synth, int pat, int step, uint8_t v);
void acid_seq_set_oct(acid_seq_t *q, int synth, int pat, int oct); // -2..+2, вне - игнор
void acid_seq_set_scale(acid_seq_t *q, int synth, int pat, int scale); // 0..ACID_SEQ_SCALES-1, вне - игнор
void acid_seq_set_row(acid_seq_t *q, int synth, int row, const uint8_t cells[ACID_SEQ_CELLS], int rep); // rep 1..16
void acid_seq_set_edit(acid_seq_t *q, int synth, int pat);
void acid_seq_set_bpm(acid_seq_t *q, int bpm); // 60..200, вне - игнор
// ▶/■. Пуск: песня в начало; beat_running - ждать шага 0 общего такта. Стоп - note off звучащей в out.
int acid_seq_run(acid_seq_t *q, int synth, int on, int beat_running, acid_seq_ev_t out[2]);
// Тик клока. beat_step - шаг общего такта, начавшийся на этом тике (0..15), иначе -1.
// Возвращает число событий в out (не больше 4: по on и off на синт).
int acid_seq_clock(acid_seq_t *q, int beat_step, const acid_tonal_t *tonal, acid_seq_ev_t out[4]);
void acid_seq_rewind(acid_seq_t *q); // START BSP и смена режима: позиции в начало (run не трогает)

// Blob во flash: магия {'S','Q','1',2}, такты, окна, лады, песня, run и edit обоих синтов, темп. Позиция не
// хранится. Версия 1 (без ладов) читается, лады - TONAL.
#define ACID_SEQ_TRACK_BLOB \
    (ACID_SEQ_PATS * ACID_SEQ_STEPS + 2 * ACID_SEQ_PATS + ACID_SEQ_ROWS * ACID_SEQ_CELLS + ACID_SEQ_ROWS + 2)
#define ACID_SEQ_BLOB (4 + ACID_SEQ_SYNTHS * ACID_SEQ_TRACK_BLOB + 1)
size_t acid_seq_export(const acid_seq_t *q, void *buf, size_t cap); // 0 - не влезло
int acid_seq_import(acid_seq_t *q, const void *buf, size_t len);    // 0 - чужая магия или длина, q не тронут

// Внутренний клок 24 PPQN, когда клока BSP нет. Счёт в сэмплах, остаток копится (без дрейфа темпа).
typedef struct {
    uint32_t acc;      // в единицах bpm * 24 сэмплов: тик - каждые sr * 60
    uint32_t ext_left; // сэмплов, пока молчим после тика BSP
} acid_seq_clk_t;
void acid_seq_clk_init(acid_seq_clk_t *c);
void acid_seq_clk_ext(acid_seq_clk_t *c, int sr); // пришёл тик BSP: 0.5 с внутренний молчит
// Кусок из n сэмплов: сколько внутренних тиков выдать при bpm (0, если клок BSP жив).
int acid_seq_clk_run(acid_seq_clk_t *c, int bpm, int n, int sr);
