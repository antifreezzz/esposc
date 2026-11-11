#pragma once
#include <stdint.h>

// Ревёрб-пластина (схема Гризингера, как в Rings и Clouds): 4 входных allpass и петля-восьмёрка из двух веток
// (ФНЧ, 2 allpass, линия с плавающим чтением). Чистый DSP, о синте не знает.
// Считается на 24 кГц кадрами по 16 тиков (32 сэмпла 48 кГц): каждая линия проходится куском, без масок индекса
// и без рекурсии внутри кадра (все линии не короче кадра). Вход - моно-посыл, выход - только wet в два канала,
// с опозданием на кадр (0.67 мс) - поэтому результат не зависит от нарезки вызовов.

#define ACID_REVERB_FRAME 16 // тиков 24 кГц в кадре
// Длины линий в тиках 24 кГц (взаимно простые): вход, ветка 1 (allpass, allpass, линия), ветка 2.
#define ACID_REVERB_IN1 53
#define ACID_REVERB_IN2 79
#define ACID_REVERB_IN3 113
#define ACID_REVERB_IN4 197
#define ACID_REVERB_A1 827
#define ACID_REVERB_A2 1019
#define ACID_REVERB_D1 1709
#define ACID_REVERB_B1 953
#define ACID_REVERB_B2 839
#define ACID_REVERB_D2 2389
#define ACID_REVERB_MEM                                                                                        \
    (ACID_REVERB_IN1 + ACID_REVERB_IN2 + ACID_REVERB_IN3 + ACID_REVERB_IN4 + ACID_REVERB_A1 + ACID_REVERB_A2 + \
     ACID_REVERB_D1 + ACID_REVERB_B1 + ACID_REVERB_B2 + ACID_REVERB_D2) // 8178 float, 32 КБ
#define ACID_REVERB_AP 0.625f    // коэффициент всех allpass
#define ACID_REVERB_MOD 32.0f    // глубина плавания чтения линий, тиков
#define ACID_REVERB_LFO1 3.1e-4f // шаг фазы за кадр: 0.47 Гц
#define ACID_REVERB_LFO2 2.1e-4f // 0.31 Гц
#define ACID_REVERB_LOWCUT 0.046f // ФВЧ посыла ~180 Гц на 24 кГц
#define ACID_REVERB_GAIN 0.5f     // выход wet

typedef struct {
    float *line[10]; // вход 1-4, A1, A2, D1, B1, B2, D2
    int p[10];       // позиция в линии (чтение и запись в одну ячейку)
    float krt, damp; // обратная связь петли и коэффициент ФНЧ
    float hp, lp1, lp2;
    float ph1, ph2;  // фазы плавания 0..1
    float y[2][ACID_REVERB_FRAME]; // выход прошлого кадра
    float yp[2];     // последний отданный тик (для середины)
    float in[ACID_REVERB_FRAME];
    float hold;      // первый сэмпл пары
    int pos;         // сэмпл 48 кГц в кадре, 0..31
    int quiet;       // тихих кадров подряд
    int state;       // 0 спит (память чистая), 1 играет, 2 спит и чистит память
    int clr;         // сколько float уже вычищено
    int decay_knob, damp_knob;
} acid_reverb_t;

// mem на ACID_REVERB_MEM float, чистится. sr только 48000. Затухание 64 (~3 с), демпфер 50 (~3 кГц).
void acid_reverb_init(acid_reverb_t *rv, float *mem);
// decay 0..127: RT60 0.3..30 с; damp 0..127: срез ФНЧ в петле 10 кГц..500 Гц.
void acid_reverb_set(acid_reverb_t *rv, int decay_knob, int damp_knob);
// Посыл send (n сэмплов 48 кГц) -> wet в wl и wr. Возврат 0: ревёрб спит, wl и wr не тронуты (wet - нули).
int acid_reverb_run(acid_reverb_t *rv, const float *send, float *wl, float *wr, int n);
int acid_reverb_active(const acid_reverb_t *rv); // 0: спит
float acid_reverb_krt(int decay_knob);
float acid_reverb_damp(int damp_knob);
