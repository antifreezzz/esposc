#pragma once

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
