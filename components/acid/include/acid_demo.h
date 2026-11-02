#pragma once
#include <stdint.h>

#include "acid_midi.h"

// Демо-трек: партитура шлёт в синт MIDI так же, как BeatStep Pro (ноты, CC страниц и энкодеров,
// клок, Start). Темп - у песни: кусков по ACID_DEMO_CHUNK сэмплов на тик клока, шестнадцатая - 6 тиков.
// 30 - 125 BPM (акид), 25 - 150 BPM (метал).
#define ACID_DEMO_CHUNK 32
#define ACID_DEMO_TICK 30 // кусков на тик клока у песни без своего темпа
#define ACID_DEMO_STEP (6 * ACID_DEMO_TICK)
#define ACID_DEMO_HELD 16 // звучащих нот одновременно на все каналы

// Биты шага ударки.
enum {
    ACID_DEMO_KICK = 1,
    ACID_DEMO_SNARE = 2,
    ACID_DEMO_CLAP = 4,
    ACID_DEMO_CHAT = 8,
    ACID_DEMO_OHAT = 16,
    ACID_DEMO_ACC = 32, // все удары шага громче
    ACID_DEMO_CRASH = 64,
    ACID_DEMO_RIDE = 128,
    ACID_DEMO_BELL = 256,
    ACID_DEMO_RIM = 1 << 9,
    ACID_DEMO_COW = 1 << 10,
    ACID_DEMO_TOM1 = 1 << 11, // томы снизу вверх: 41, 43, 45, 47, 48, 50
    ACID_DEMO_TOM2 = 1 << 12,
    ACID_DEMO_TOM3 = 1 << 13,
    ACID_DEMO_TOM4 = 1 << 14,
    ACID_DEMO_TOM5 = 1 << 15,
    ACID_DEMO_TOM6 = 1 << 16,
};

// Нота шага: len в шестнадцатых, 0 - пауза. Нота следующего шага до off этой - слайд (перекрытие).
typedef struct {
    uint8_t note, vel, len;
} acid_demo_note_t;

// Ручка: энкодер с 1, как на BSP. from == to - один раз в начале части, иначе плавно за часть.
// vary с круга 1: N - концы плавной ручки плавают на +-N; ACID_DEMO_PICK - одно случайное значение
// между from и to на часть (круг 0 - from, to может быть и меньше from).
#define ACID_DEMO_PICK 0xFF
#define ACID_DEMO_KNOBS 48 // ручек в части не больше
typedef struct {
    uint8_t page, enc, from, to, vary;
} acid_demo_knob_t;

typedef struct {
    uint8_t bars;
    const acid_demo_note_t *syn1, *syn2; // 16 шагов, повтор каждый такт; NULL - молчит
    const uint32_t *drums;               // 16 шагов, биты ACID_DEMO_*; NULL - молчит
    const acid_demo_knob_t *knobs;
    uint8_t nknobs;
    const uint8_t *syn2_pool; // модели синта 2: круг 0 - pool[0], дальше случайно из пула
    uint8_t npool;
    uint8_t vary_syn1;        // с круга 1: 1-3 переворота акцента или слайда в нотах синта 1
    uint32_t first;           // биты ACID_DEMO_* только на первом шаге части (крэш на входе)
} acid_demo_part_t;

typedef struct {
    const acid_demo_part_t *parts;
    int nparts;
    int tick; // кусков на тик клока; 0 - ACID_DEMO_TICK
} acid_demo_song_t;

typedef struct {
    const acid_demo_song_t *song;
    int32_t tick, step; // кусков на тик и на шестнадцатую
    int part;          // текущая часть
    int32_t t;         // кусок от начала круга
    int32_t k;         // кусок внутри части
    int32_t part_len;  // длина части в кусках
    int cycle;         // номер круга
    uint32_t rnd;      // вариации круга
    int page;          // последняя посланная страница, -1 - неизвестна
    uint8_t last[ACID_DEMO_KNOBS];             // последнее посланное значение ручки части (0xFF - не слали)
    uint8_t from[ACID_DEMO_KNOBS], to[ACID_DEMO_KNOBS]; // ручки части с вариациями круга
    acid_demo_note_t syn1[16];                 // ноты синта 1 части с вариациями круга
    struct {
        uint8_t ch, note;
        int32_t off; // кусок части, на котором off (-1 - свободно)
    } held[ACID_DEMO_HELD];
} acid_demo_t;

extern const acid_demo_song_t acid_demo_song;  // демо «акид», 125 BPM
extern const acid_demo_song_t acid_metal_song; // демо «метал», 150 BPM
extern const acid_demo_song_t acid_stress_song; // стенд замеров: худший случай рендера без передышек
extern const acid_demo_song_t acid_stress_layers_song; // стенд защиты: стресс и тяжёлые слои на обоих синтах

void acid_demo_init(acid_demo_t *d, const acid_demo_song_t *song);
// События следующего куска в out (не больше cap), возврат - число. Порядок: Start, CC, note off, note on, клок.
int acid_demo_events(acid_demo_t *d, acid_midi_event_t *out, int cap);
int acid_demo_cycle(const acid_demo_t *d);
