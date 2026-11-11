#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pl_grids.h"

// Кадры UART синт <-> панель: 0xA5, тип, длина, полезная нагрузка, CRC-8 (полином 0x07 по типу, длине и нагрузке).
// Битый кадр выбрасывается, приём ищет следующий синхробайт (в том числе внутри выброшенного).
// Длина у каждого типа своя: заголовок с чужой длиной - не кадр, отбрасывается сразу.

#define PL_SYNC 0xA5
#define PL_PAGES 14 // 0-9 штатные, 10-11 слои синтов, 12 Grids, 13 чашка Петри
#define PL_PAGE_SYNTH1 6 // синт 2 - следом
#define PL_PAGE_LAYER1 10
#define PL_PAGE_GRIDS 12
#define PL_PAGE_PETRI 13 // на BSP 18
#define PL_PAGE_REVERB 27 // подэкран FX, на BSP номера нет
#define PL_REVERB_KNOBS 6
#define PL_KNOBS 16
#define PL_PAYLOAD_MAX 255
#define PL_FRAME_MAX (PL_PAYLOAD_MAX + 4)
#define PL_USER_PAGES 6 // свои страницы 14..19 (на BSP 10..15)
#define PL_USER_FIRST 14
// Встроенные страницы псевдонимов (вкладки MIX инструментов): следом за своими, номера BSP нет.
#define PL_MIX_PAGES 3
#define PL_PAGE_MIX_DRUMS (PL_USER_FIRST + PL_USER_PAGES) // 20
#define PL_PAGE_MIX_SYN1 (PL_PAGE_MIX_DRUMS + 1)          // 21, синт 2 - следом
// Модуляторы: страницы 23..26 внутри, на BSP 19..22.
#define PL_MOD_PAGES 4
#define PL_PAGE_MOD1 (PL_PAGE_MIX_DRUMS + PL_MIX_PAGES) // 23
#define PL_BSP_MOD_FIRST 19
#define PL_MOD_TARGETS 4
// Кадр PL_MOD_POS: выходы модуляторов и следом место каждого в периоде - у LFO фаза 0..255, у огибающей
// (форма PL_MOD_SHAPE_ENV) стадия: 0 молчит, 1 атака, 2 спад.
#define PL_MOD_POS_LEN (2 * PL_MOD_PAGES)
#define PL_MOD_SHAPE_ENV 7
#define PL_TARGETS 4
#define PL_NONE 0xFF
#define PL_TYPE_PETRI 25 // тип синта (энкодер 2 страниц 6-7): чашка Петри, у слоя не бывает
#define PL_LAYER_OFF 26  // тип слоя «выкл»

enum {
    PL_SNAPSHOT = 1, // синт -> панель: всё состояние
    PL_KNOB = 2,     // синт -> панель: одна ручка или смена страницы BSP (enc 0)
    PL_SET = 3,      // панель -> синт: поставить ручку
    PL_HELLO = 4,    // панель -> синт: пришли снимок
    PL_PAGE = 5,     // панель -> синт: открыта страница, энкодеры BSP крутят её (1 байт)
    PL_CUSTOM = 6,      // синт -> панель: определения и значения одной своей страницы
    PL_CUSTOM_EDIT = 7, // панель -> синт: поставить или убрать цель слота
    PL_SCENE = 8,       // панель -> синт: вызвать или записать сцену
    PL_SCENE_STATE = 9, // синт -> панель: занятые слоты, текущая сцена, переход
    PL_LOAD = 10,       // синт -> панель: загрузка ядер (pl_load.h)
    PL_DEMO = 11,       // панель -> синт: следующая песня демо (без нагрузки; в live синт игнорирует)
    PL_GRIDS_STEP = 12, // синт -> панель: шаг паттерна Grids 0..31, 0xFF - стоит (1 байт)
    PL_SNAPSHOT2 = 13,  // синт -> панель: вторая часть снимка (позиции энкодеров BSP, подхват), сразу за PL_SNAPSHOT
    PL_PETRI_0 = 14,    // панель -> синт: таблица чашки Петри, части 0..3 (pl_petri_split)
    PL_PETRI_1 = 15,
    PL_PETRI_2 = 16,
    PL_PETRI_3 = 17,
    PL_SEQ_STEP = 18, // панель -> синт: шаг такта {synth, pat, step, v}
    PL_SEQ_OCT = 19,  // панель -> синт: окно такта {synth, pat, oct + 2}
    PL_SEQ_ROW = 20,  // панель -> синт: строка песни {synth, row, cell[8], rep}
    PL_SEQ_CTRL = 21, // панель -> синт: ▶/■ и такт экрана {synth, run, edit}; run PL_SEQ_KEEP - не трогать
    PL_SEQ_BPM = 22,  // панель -> синт: темп внутреннего клока {bpm}
    PL_SEQ_DUMP = 23, // синт -> панель: всё одного синта (pl_seq_dump_t), после PL_HELLO
    PL_SEQ_POS = 24,  // синт -> панель: позиция игры и темп (pl_seq_pos_t), при изменении
    PL_SEQ_SCALE = 25, // панель -> синт: лад такта {synth, pat, scale} (0 - со страницы 9, 1 + k - лад k)
    PL_MODE = 26,      // панель -> синт: поставить режим (1 байт: PL_DEMO_* без BSP, с BSP - PL_MODE_LIVE или PL_DEMO_DRONE)
    PL_GRIDS_PAT = 27,  // обе стороны: свой рисунок Grids целиком (pl_grids_pat_frame_t); синт шлёт все после PL_HELLO
    PL_GRIDS_CELL = 28, // панель -> синт: клетка своего рисунка {slot, part, step, уровень}
    PL_MOD = 29,        // синт -> панель: ручки, цели и подхват одного модулятора (pl_mod_t)
    PL_MOD_EDIT = 30,   // панель -> синт: поставить или убрать цель модулятора (pl_mod_edit_t)
    PL_MOD_POS = 31,    // синт -> панель: 4 байта out (int8, -127..127) и 4 байта места в периоде (PL_MOD_POS_LEN)
};

// Свой рисунок Grids в кадре: слот U1..U4 и рисунок (сетка и 96 уровней).
typedef struct {
    uint8_t slot;
    pl_grids_pat_t pat;
} pl_grids_pat_frame_t;

// Секвенсер: байт шага - ступень 0..14 (PL_SEQ_REST - пауза) и флаги; совпадает с ACID_SEQ_* синта.
#define PL_SEQ_SYNTHS 2
#define PL_SEQ_PATS 8
#define PL_SEQ_STEPS 16
#define PL_SEQ_ROWS 8
#define PL_SEQ_CELLS 8
#define PL_SEQ_REST 0x0F
#define PL_SEQ_DEG 0x0F
#define PL_SEQ_ACC 0x10
#define PL_SEQ_SLIDE 0x20
#define PL_SEQ_UP 0x40 // нота на октаву выше
#define PL_SEQ_SCALES 9
#define PL_SEQ_EMPTY 0xFF
#define PL_SEQ_KEEP 0xFF // run в PL_SEQ_CTRL: только такт экрана, ▶/■ как есть
typedef struct {
    uint8_t synth;
    uint8_t pat[PL_SEQ_PATS][PL_SEQ_STEPS];
    uint8_t oct[PL_SEQ_PATS]; // окно + 2 (0..4)
    uint8_t scale[PL_SEQ_PATS];
    uint8_t cell[PL_SEQ_ROWS][PL_SEQ_CELLS], rep[PL_SEQ_ROWS];
    uint8_t run, edit, bpm;
} pl_seq_dump_t;
typedef struct {
    uint8_t synth, pat, step, row, cell, n, run, bpm; // n - проход строки; bpm - темп, по которому идём
} pl_seq_pos_t;

// Таблица чашки (256 x int16, little-endian) в 4 кадрах по 64 точки: номер таблицы + точки; в последнем ещё
// 6 модуляций, счётчик прыжков и флаги. Нагрузка кадра не больше 255 байт, вся таблица - 512.
#define PL_PETRI_POINTS 256
#define PL_PETRI_PART 129      // номер + 64 точки
#define PL_PETRI_PART_MAX 137  // последняя: + 6 модуляций, прыжки, флаги
typedef struct {
    uint8_t seq, have; // номер собираемой таблицы, биты пришедших частей
    int16_t tab[PL_PETRI_POINTS];
    int8_t mods[6];
    uint8_t jumps, flags;
} pl_petri_rx_t;
// Синт с типом PETRI по сырым значениям: 0 или 1 (оба - синт 1), -1 - ни один.
int pl_petri_synth(const uint8_t raw[PL_PAGES][PL_KNOBS]);
// Нагрузки 4 кадров (типы PL_PETRI_0 + k) и их длины.
void pl_petri_split(const int16_t tab[PL_PETRI_POINTS], const int8_t mods[6], uint8_t jumps, uint8_t seq,
                    uint8_t out[4][PL_PETRI_PART_MAX], size_t len[4]);
// Часть таблицы; 1 - собраны все 4 части одного номера (таблица в r). Часть другого номера начинает сборку заново.
int pl_petri_collect(pl_petri_rx_t *r, uint8_t type, const uint8_t *payload, size_t len);

// Песня демо, режим дрона или режим секвенсера в снимке.
enum { PL_DEMO_METAL, PL_DEMO_ACID, PL_DEMO_DRONE, PL_DEMO_SEQ, PL_DEMO_SONGS = PL_DEMO_DRONE };
#define PL_MODE_LIVE 4 // PL_MODE с BSP: играть с BSP (не дрон); вне PL_DEMO_*, чтобы не спутать с песней
// Годится ли режим mode для PL_MODE: без BSP (live 0) - любой PL_DEMO_*, с BSP - PL_MODE_LIVE или PL_DEMO_DRONE.
int pl_mode_ok(int live, int mode);

// Нагрузки - только байты, без выравнивания.
typedef struct {
    uint8_t mode;     // 0 демо, 1 live
    uint8_t bsp_page; // страница BSP
    uint8_t raw[PL_PAGES][PL_KNOBS];
    uint8_t demo; // песня демо PL_DEMO_*
} pl_snapshot_t;

// Вторая часть снимка: с 14-й страницей весь снимок не влезает в 255 байт кадра.
typedef struct {
    uint8_t bsp_pos[PL_KNOBS];                // 0xFF - энкодер не крутили
    uint8_t held[(PL_PAGES * PL_KNOBS) / 8]; // бит page * 16 + enc: ручка ждёт подхвата BSP
    uint8_t reverb[PL_REVERB_KNOBS];         // посылы S1, S2, ударки, DECAY, DAMP, WIDTH
} pl_snapshot2_t;

typedef struct {
    uint8_t page, enc, value, src, bsp_pos, held; // src: 0 BSP, 1 демо, 2 панель
} pl_knob_t;

typedef struct {
    uint8_t page, enc, value;
} pl_set_t;

// Цель слота своей страницы: pe = страница << 4 | энкодер (с 0), PL_NONE - пусто.
typedef struct {
    uint8_t pe, lo, hi;
} pl_target_t;

typedef struct {
    uint8_t up;                              // своя страница 0..5
    pl_target_t t[PL_KNOBS - 1][PL_TARGETS]; // слоты энкодеров 1..15 (индексы)
    uint8_t val[PL_KNOBS - 1];               // значение слота (у псевдонима - цели)
    uint8_t held[2];                         // бит enc - 1: слот ждёт подхвата BSP
} pl_custom_t;

typedef struct {
    uint8_t up, enc, idx, pe, lo, hi; // pe PL_NONE - убрать цель idx
} pl_custom_edit_t;

typedef struct {
    uint8_t page, enc; // page PL_NONE - пусто
} pl_mod_target_t;

typedef struct {
    uint8_t mod;                       // модулятор 0..3
    uint8_t knob[PL_KNOBS];            // значения ручек модулятора
    pl_mod_target_t t[PL_MOD_TARGETS]; // цели 0..3
    uint8_t held[2];                   // бит enc - 1: ручка ждёт подхвата BSP
} pl_mod_t;

typedef struct {
    uint8_t mod, idx, page, enc; // page PL_NONE - убрать цель idx
} pl_mod_edit_t;

enum { PL_SCENE_RECALL, PL_SCENE_STORE };
typedef struct {
    uint8_t op, n, bars;
} pl_scene_t;

typedef struct {
    uint8_t used, cur, state; // cur PL_NONE - не вызывали; state 0 нет, 1 ждём такт, 2 идёт
} pl_scene_state_t;

// Кадр в out (не меньше 4 + len), возвращает его длину; len больше PL_PAYLOAD_MAX - 0.
// Номер страницы, как на экране BSP: свои 10-15, слои 16-17, чашка 18, у Grids нет (-1); номера страниц в кадрах -
// внутренние.
int pl_page_bsp(int page);

size_t pl_encode(uint8_t type, const void *payload, size_t len, uint8_t *out);

typedef struct {
    uint8_t buf[PL_FRAME_MAX + 1];
    size_t n;    // байт в buf
    size_t used; // длина выданного кадра: убрать перед следующим разбором
} pl_rx_t;

void pl_rx_init(pl_rx_t *r);
// Байт с линии; 1 - собран кадр (payload живёт до следующего вызова pl_rx_*).
int pl_rx_feed(pl_rx_t *r, uint8_t byte, uint8_t *type, const uint8_t **payload, size_t *len);
// Следующий уже собранный кадр без нового байта (после ложного синхробайта их может быть несколько).
int pl_rx_next(pl_rx_t *r, uint8_t *type, const uint8_t **payload, size_t *len);
