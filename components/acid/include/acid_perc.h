#pragma once
#include <stdint.h>

// Перкуссия синтезом: набор томов (41, 43, 45, 47, 48, 50), римшот 37, ковбелл 56.
// Один голос на все модели: два генератора, шум, огибающие громкости и высоты, 4 полосовых резонатора,
// перегруз и крашер. Модель только переключает, как они соединены (кэш команд 32 КБ: один код, не пять).
enum { ACID_PERC_TOMS, ACID_PERC_RIM, ACID_PERC_COW, ACID_PERC_N };
enum { ACID_PERC_ANALOG, ACID_PERC_SQUARES, ACID_PERC_FM, ACID_PERC_MODAL, ACID_PERC_NOISE, ACID_PERC_MODELS };
// Ручка = энкодер BSP - 2 (энкодер 2 -> 0).
enum {
    ACID_PERC_MODEL, ACID_PERC_TUNE, ACID_PERC_DECAY, ACID_PERC_PDEPTH, ACID_PERC_PTIME, ACID_PERC_TONE,
    ACID_PERC_CHAR, ACID_PERC_CLICK, ACID_PERC_DRIVE, ACID_PERC_CRUSH, ACID_PERC_LEVEL, ACID_PERC_PAN,
    ACID_PERC_SENS, ACID_PERC_INTERVAL, ACID_PERC_WIDTH, ACID_PERC_KNOBS
};
#define ACID_PERC_SOUNDING 3 // звучащих голосов (палочек две, плюс хвост)
#define ACID_PERC_SLOTS 8    // вместе с уходящими: удар никогда не стирает слышный голос
#define ACID_PERC_MODES 4

// Инструмент: сырые ручки и то, что из них посчитано.
typedef struct {
    uint8_t knob[ACID_PERC_KNOBS];
    int model;
    float hz;               // строй (у томов - самый низкий)
    float amp_coef;         // спад громкости на сэмпл
    float pdepth;           // глубина высоты, полутоны
    float pitch_coef;       // спад высоты на сэмпл
    float tone, chr, click; // 0..1
    float drive;            // усиление перед ограничением, 1 - чисто
    int hold;               // крашер: держать отсчёт столько сэмплов, 1 - выкл
    float quant;            // крашер: шаг квантования, 0 - выкл
    float gain;
    float sens;
    float interval;         // полутоны между томами
    float width;            // 0..1
} acid_perc_inst_t;

typedef struct {
    uint8_t active, inst, note;
    int8_t tom;             // 0..5 или -1
    float vel;              // сила удара 0..1 (с полом ударки)
    float amp;              // громкость удара с чувствительностью
    float env, penv, cenv;  // громкость, высота, щелчок
    float fade, fade_step;  // уход: fade от 1 к 0, шаг 0 - не уходит
    float pa, pb;           // фазы генераторов 0..1
    float res[ACID_PERC_MODES][2]; // состояния резонаторов
    float lp;               // фильтр возбудителя и цвета шума
    float held;             // крашер
    int hold_left;
    uint32_t rng;
} acid_perc_voice_t;

typedef struct {
    acid_perc_inst_t inst[ACID_PERC_N];
    acid_perc_voice_t v[ACID_PERC_SLOTS];
    float sr;
    float click_coef, exc_coef; // спад щелчка и возбудителя на сэмпл
    uint32_t seed;
    int limit;          // звучащих голосов не больше (перегруз - меньше)
    uint32_t hard_cuts; // слышный голос стёрт без ухода (для тестов, должно быть 0)
} acid_perc_t;

void acid_perc_init(acid_perc_t *p, float sr); // ручки по умолчанию, голоса пустые
// v - число на экране BSP. Модель больше 4 игнорируется, интервал и ширина есть только у томов.
void acid_perc_set(acid_perc_t *p, int inst, int knob, int v);
// Инструмент ноты или -1; tom (если не NULL) - индекс тома 0..5 или -1.
int acid_perc_inst_of(int note, int *tom);
// v 0..1, уже с полом ударки. Не перкуссия - ничего.
void acid_perc_trigger(acid_perc_t *p, int note, float v);
// Кусок n сэмплов в l, r (перезаписывает). Голос за голосом, ручки действуют и на звучащие голоса.
void acid_perc_render(acid_perc_t *p, float *l, float *r, int n);
// Звучащих голосов не больше n (1..ACID_PERC_SOUNDING): лишние самые тихие сразу уходят за 5 мс.
void acid_perc_set_limit(acid_perc_t *p, int n);
int acid_perc_voices(const acid_perc_t *p);   // занятые слоты
int acid_perc_sounding(const acid_perc_t *p); // занятые и не уходящие
