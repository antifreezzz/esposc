#pragma once
#include <stddef.h>
#include <stdint.h>

// Модуляторы: 4 общих LFO или огибающих, у каждого до 4 целей (ручек синта) с глубиной со знаком.
// Живут вне acid_synth_t (как секвенсер), шаг раз в кусок в фазе событий. В рендере их нет.
// Спека: docs/superpowers/specs/2026-11-10-modulators-design.md.

#define ACID_MODS 4
#define ACID_MOD_TARGETS 4
#define ACID_MOD_KNOBS 16
#define ACID_MOD_NONE 0xFF // цели нет

// Ручки страницы модулятора: номер энкодера с 0 (0 - страница BSP).
enum {
    ACID_MOD_SHAPE = 1,
    ACID_MOD_RATE,
    ACID_MOD_SYNC,
    ACID_MOD_TRIG,
    ACID_MOD_PROB,
    ACID_MOD_ATTACK,
    ACID_MOD_DECAY,
    ACID_MOD_AMOUNT,
    ACID_MOD_DEPTH1, // .. DEPTH1 + 3
};

enum { ACID_SHAPE_SIN, ACID_SHAPE_TRI, ACID_SHAPE_SAW, ACID_SHAPE_RAMP, ACID_SHAPE_SQR, ACID_SHAPE_SH,
       ACID_SHAPE_DRIFT, ACID_SHAPE_ENV, ACID_SHAPES };
#define ACID_MOD_SYNCS 9  // FREE, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8 тактов
#define ACID_MOD_TRIGS 14 // FREE и 13 источников

// Запуски: значение ручки TRIG k (1..13) слушает бит k - 1.
enum {
    ACID_TRIG_NOTE1 = 1 << 0,
    ACID_TRIG_NOTE2 = 1 << 1,
    ACID_TRIG_ACC1 = 1 << 2,
    ACID_TRIG_ACC2 = 1 << 3,
    ACID_TRIG_KICK = 1 << 4,
    ACID_TRIG_SNARE = 1 << 5,
    ACID_TRIG_HAT = 1 << 6,
    ACID_TRIG_CLAP = 1 << 7,
    ACID_TRIG_PERC = 1 << 8,
    ACID_TRIG_BEAT = 1 << 9,
    ACID_TRIG_BAR = 1 << 10,
    ACID_TRIG_BAR4 = 1 << 11,
    ACID_TRIG_STEP = 1 << 12,
};

typedef struct {
    uint8_t page, enc; // page ACID_MOD_NONE - пусто
} acid_mod_target_t;

typedef struct {
    uint8_t knob[ACID_MOD_KNOBS];
    acid_mod_target_t t[ACID_MOD_TARGETS];
    float phase, out;       // фаза 0..1, выход: LFO -1..1, ENV 0..1
    float inc;              // шаг фазы за кусок при свободном ходе
    float held, goal;       // S&H: значение; DRIFT: цель хода
    float att_inc, dec_k;   // огибающая: шаг атаки, множитель спада за кусок
    int stage;              // огибающая: 0 молчит, 1 атака, 2 спад
} acid_mod_one_t;

typedef struct {
    acid_mod_one_t m[ACID_MODS];
    float dt;         // секунд на шаг
    float tick_s;     // оценка длины тика клока
    float since_tick; // секунд с последнего тика
    uint32_t pos;     // позиция клока в тиках, как в прошлый шаг
    uint32_t rnd;
    int save;         // ручки или цели поменялись: пора записать
    int next;         // с какой цели продолжать применение (по кругу)
} acid_mod_t;

#define ACID_MOD_BLOB (4 + ACID_MODS * (ACID_MOD_KNOBS + 2 * ACID_MOD_TARGETS))
#define ACID_MOD_PER_CHUNK 1

struct acid_synth;

void acid_mod_init(acid_mod_t *m, float dt);
// Ручка enc 1..15 модулятора k. 1 - принято (списки берут только свои значения, как экран BSP).
int acid_mod_set(acid_mod_t *m, int k, int enc, int v);
int acid_mod_knob(const acid_mod_t *m, int k, int enc);
// Годится ли ручка синта целью по номеру страницы (плавность ручки проверяет тот, кто знает тип синта).
int acid_mod_page_ok(int page, int enc);
// Поставить цель idx (page ACID_MOD_NONE - убрать). 1 - принято.
int acid_mod_target(acid_mod_t *m, int k, int idx, int page, int enc);
// Шаг всех модуляторов на один кусок. trig - биты ACID_TRIG_* за кусок, pos - позиция клока в тиках (24 на
// четверть; не меняется - клок стоит).
void acid_mod_run(acid_mod_t *m, unsigned trig, uint32_t pos);
float acid_mod_out(const acid_mod_t *m, int k);
// Сдвиг цели idx модулятора k в единицах ручки.
int acid_mod_offset(const acid_mod_t *m, int k, int idx);
// Сумма сдвигов всех модуляторов на ручку; *n - сколько целей на неё назначено (можно NULL).
int acid_mod_sum(const acid_mod_t *m, int page, int enc, int *n);
int acid_mod_take_save(acid_mod_t *m);
void acid_mod_export(const acid_mod_t *m, void *buf);
int acid_mod_import(acid_mod_t *m, const void *buf, size_t len); // 0 - чужой blob, ничего не тронуто
// Шаг модуляторов и применение изменившихся целей (не больше budget и ACID_MOD_PER_CHUNK, на защите >= 1 - стоп).
int acid_mod_step(acid_mod_t *m, struct acid_synth *s, int budget);
