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
    float g, k, g_target, k_target; // сглаживаются к цели посэмплово
    int mode, next_mode;            // 0 обход, 1 ФНЧ, 2 ФВЧ
    float wet;                      // 0..1, доля фильтра в кроссфейде
    float smooth, xfade;            // коэффициент сглаживания и шаг кроссфейда
    int knob, res_knob;
} acid_djf_t;

void acid_djf_init(acid_djf_t *f, float sr);              // ручка 64, резонанс 0: обход
void acid_djf_set(acid_djf_t *f, int knob, int res_knob); // из обработки CC
float acid_djf_next(acid_djf_t *f, float in);
int acid_djf_active(const acid_djf_t *f);                 // 0: обход и кроссфейд закончен
float acid_djf_cutoff_hz(int knob);                       // 0 в мёртвой зоне 62..66
float acid_djf_q(int knob, int res_knob);

// Хруст: меньше разрядность и удержание сэмпла (понижение частоты). Обе ручки 0 - выключен.
typedef struct {
    int bits_knob, rate_knob;
    float step, inv_step; // шаг квантования, 0 - без квантования
    int factor, cnt;      // удерживать сэмпл factor раз
    float hold;
} acid_crush_t;

void acid_crush_init(acid_crush_t *c);
void acid_crush_set(acid_crush_t *c, int bits_knob, int rate_knob); // 0..127 каждая
float acid_crush_next(acid_crush_t *c, float x);
int acid_crush_active(const acid_crush_t *c);

// Дилэй по темпу: буфер 16 бит во внутренней RAM, ФНЧ в петле, время плывёт как у ленты.
#define ACID_DELAY_LEN 36000 // 0.75 с при 48 кГц

typedef struct {
    float sr;
    int16_t buf[ACID_DELAY_LEN];
    int w;           // куда пишем
    float spb;       // сэмплов на четверть
    int time_knob;
    float fb;        // обратная связь 0..0.95
    float tone;      // коэффициент ФНЧ в петле
    float lp;        // состояние ФНЧ
    float d, d_target; // задержка в сэмплах: текущая и цель
    int zero_run;    // нулевых записей подряд; >= ACID_DELAY_LEN - буфер весь в нулях
} acid_delay_t;

void acid_delay_init(acid_delay_t *d, float sr); // 120 BPM, 3/16, fb 0.4, тон 4 кГц; буфер чистится
void acid_delay_set(acid_delay_t *d, int time_knob, int fb_knob, int tone_knob);
void acid_delay_set_spb(acid_delay_t *d, float spb);
float acid_delay_next(acid_delay_t *d, float in); // in - сумма посылов, возврат - повторы
int acid_delay_active(const acid_delay_t *d);     // 0: в буфере одни нули, next можно не звать
float acid_delay_time_samples(const acid_delay_t *d); // целевая задержка
