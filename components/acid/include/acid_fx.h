#pragma once
#include <stdint.h>

// Эффекты мастера: пампинг от бочки и DJ-фильтр. Чистый DSP, о синте не знает.

// Пампинг: огибающая e 0..1, множитель громкости 1 - depth * e.
typedef struct {
    float sr;
    float e, target;      // огибающая и цель атаки (velocity удара)
    float atk_coef, rel_coef; // шаг линейной атаки и множитель спада
    float depth, depth_s; // глубина с ручки и сглаженная
    float smooth;         // коэффициент сглаживания глубины
} acid_pump_t;

void acid_pump_init(acid_pump_t *p, float sr);           // глубина 0, возврат 0.15 с
void acid_pump_set_depth(acid_pump_t *p, float depth);   // 0..1
void acid_pump_set_release(acid_pump_t *p, float time_s); // время почти полного возврата
void acid_pump_hit(acid_pump_t *p, float vel);           // vel 0..1: цель max(target, vel)
float acid_pump_next(acid_pump_t *p);                    // множитель на сэмпл
int acid_pump_active(const acid_pump_t *p);              // 0: глубина ноль, next можно не звать

// DJ-фильтр: TPT SVF, ФНЧ или ФВЧ, в центре ручки обход с кроссфейдом.
typedef struct {
    float sr;
    float ic1, ic2;
    float ic1r, ic2r;               // состояние правого канала
    float g, k, g_target, k_target; // сглаживаются к цели посэмплово
    float a1, a2, a3;               // коэффициенты SVF от g и k: деление только пока срез едет
    int mode, next_mode;            // 0 обход, 1 ФНЧ, 2 ФВЧ
    float wet;                      // 0..1, доля фильтра в кроссфейде
    float smooth, xfade;            // коэффициент сглаживания и шаг кроссфейда
    int knob, res_knob;
} acid_djf_t;

void acid_djf_init(acid_djf_t *f, float sr);              // ручка 64, резонанс 0: обход
void acid_djf_set(acid_djf_t *f, int knob, int res_knob); // из обработки CC
float acid_djf_next(acid_djf_t *f, float in);
void acid_djf_next2(acid_djf_t *f, float *l, float *r); // общие коэффициенты, своё состояние у каналов
int acid_djf_active(const acid_djf_t *f);                 // 0: обход и кроссфейд закончен
float acid_djf_cutoff_hz(int knob);                       // 0 в мёртвой зоне 62..66
float acid_djf_q(int knob, int res_knob);

// Хруст: меньше разрядность и удержание сэмпла (понижение частоты). Обе ручки 0 - выключен.
typedef struct {
    int bits_knob, rate_knob;
    float step, inv_step; // шаг квантования, 0 - без квантования
    int factor, cnt;      // удерживать сэмпл factor раз
    float hold, hold_r;
} acid_crush_t;

void acid_crush_init(acid_crush_t *c);
void acid_crush_set(acid_crush_t *c, int bits_knob, int rate_knob); // 0..127 каждая
float acid_crush_next(acid_crush_t *c, float x);
void acid_crush_next2(acid_crush_t *c, float *l, float *r); // один счётчик удержания на оба канала
int acid_crush_active(const acid_crush_t *c);

// Дилэй по темпу: буфер 16 бит (на плате в PSRAM), ФНЧ в петле, время плывёт как у ленты.
#define ACID_DELAY_LEN 36864 // 0.77 с: 3/8 на 120 BPM (36000) влезает

typedef struct {
    float sr;
    int16_t *buf;    // 2 * ACID_DELAY_LEN: линия A, затем линия B
    int w;           // куда пишем
    float spb;       // сэмплов на четверть
    int time_knob;
    float fb;        // обратная связь 0..0.95
    float tone;      // коэффициент ФНЧ в петле
    float lp;        // состояние ФНЧ
    float d, d_target; // задержка в сэмплах: текущая и цель
    int zero_run;    // нулевых записей подряд (обе линии); >= ACID_DELAY_LEN - буферы в нулях
    float lp_b;      // ФНЧ линии B
    int motion;      // 0 центр, 1 пинг-понг, 2 случайная точка на повтор
    int width;       // ширина повторов 0..127
    float m;         // доля пинг-понга в петле, плывёт 0..1
    float pos_a, pos_b, tgt_a, tgt_b; // панорама выходов линий: текущая и цель
    int rnd_left;    // сэмплов до новой случайной точки
    uint32_t rnd;
} acid_delay_t;

// 120 BPM, 3/16, fb 0.4, тон 4 кГц; buf на 2 * ACID_DELAY_LEN чистится.
void acid_delay_init(acid_delay_t *d, float sr, int16_t *buf);
void acid_delay_set(acid_delay_t *d, int time_knob, int fb_knob, int tone_knob);
void acid_delay_set_spb(acid_delay_t *d, float spb);
float acid_delay_next(acid_delay_t *d, float in); // моно: среднее каналов acid_delay_next2
// in - сумма посылов (моно), повторы в два канала по движению.
void acid_delay_next2(acid_delay_t *d, float in, float *l, float *r);
void acid_delay_set_motion(acid_delay_t *d, int motion, int width); // motion > 2 игнор, width 0..127
int acid_delay_active(const acid_delay_t *d);     // 0: в буфере одни нули, next можно не звать
float acid_delay_time_samples(const acid_delay_t *d); // целевая задержка

// Статтер: записать кусок доли такта и крутить по кругу, пока ручка не в 0.
#define ACID_STUTTER_LEN 24576 // 0.51 с: 1/4 на 120 BPM (24000) и шов влезают
#define ACID_STUTTER_SEAM 96   // 2 мс кроссфейда на шве петли

typedef struct {
    int16_t *buf;    // 2 * ACID_STUTTER_LEN: левый, затем правый
    float spb;
    int knob;
    int on;          // запись или петля (ручка не в 0)
    int looping;     // кусок записан, крутим
    int len;         // длина петли
    int rec;         // сколько записано (до len + шов)
    int pos;         // позиция в петле
    int restart;     // ждём ухода петли в 0, потом пишем заново длиной new_len
    int new_len;
    int opos, xf;    // старая головка при укорочении и сколько сэмплов ещё сводить с ней
    float wet, xstep; // доля петли в выходе и её шаг (2 мс)
} acid_stutter_t;

void acid_stutter_init(acid_stutter_t *st, float sr, int16_t *buf); // buf на 2 * ACID_STUTTER_LEN
void acid_stutter_set(acid_stutter_t *st, int knob); // 0 выкл, 1..127 - 4 зоны: 1/4, 1/8, 1/16, 1/32
void acid_stutter_set_spb(acid_stutter_t *st, float spb);
float acid_stutter_next(acid_stutter_t *st, float in); // моно: оба канала одинаковые, возврат левого
void acid_stutter_next2(acid_stutter_t *st, float *l, float *r);
int acid_stutter_active(const acid_stutter_t *st);

// Панорама 0 слева, 64 центр, 127 справа: в центре оба канала 1, к краю дальний канал уходит в 0.
void acid_pan(float pos, float *gl, float *gr);
