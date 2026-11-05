#pragma once
#include <stdint.h>

// Голос чашки Петри (тип синта PETRI): 4 голоса читают одну таблицу волны, которую снимает чашка на панели
// (или зашитые таблицы без неё). Новая таблица ложится в почтовый ящик в фазе событий, голос сам забирает её
// в рендере после конца кроссфейда: ящик и рендер слота никогда не идут одновременно. Фильтр и перегруз - у слота.

#define ACID_PETRI_VOICES 4
#define ACID_PETRI_TABLE 256
#define ACID_PETRI_LEVELS 4 // копии таблицы: исходная и средние по 2, 4, 8 точкам (высокие ноты без алиасинга)
#define ACID_PETRI_MODS 6   // pop, dk, vort, energy, jump, dens

typedef struct {
    int16_t t[ACID_PETRI_LEVELS][ACID_PETRI_TABLE + 1]; // + копия точки 0 для интерполяции
} acid_petri_wave_t;

typedef struct {
    int16_t tab[ACID_PETRI_TABLE];
    int8_t mods[ACID_PETRI_MODS]; // ±127 = ±1
    uint8_t jumps;                // счётчик прыжков сеятелей, растёт по кругу
} acid_petri_frame_t;

typedef struct {
    uint32_t ph, inc;
    float hz, env, x1, y1, lvl, g;
    int8_t note; // нота MIDI: отпускание по ней, частоту мог сдвинуть слой тональности
    uint8_t gate, on, level;
    uint32_t age;
} acid_petri_voice_t;

typedef struct {
    float sr;
    acid_petri_wave_t w[2], fz; // w[a] уходит, w[b] приходит; fz - фриз
    int a, b;
    acid_petri_frame_t inbox;
    int pending, inbox_slow; // ящик полон; зашитая таблица - медленный кроссфейд
    float x, dx;             // вес кроссфейда A -> B и шаг за сэмпл
    float dx_live, dx_slow;
    float m, mt, mc;         // фриз: доля, цель, коэффициент 30 мс
    acid_petri_voice_t v[ACID_PETRI_VOICES];
    uint32_t age, rnd;
    float ea, er; // коэффициенты атаки и релиза
    int ctrl;     // сэмплов до пересчёта усилений
    int8_t mods[ACID_PETRI_MODS];
    uint8_t jumps, jumps_seen;
    uint32_t quiet, quiet_max; // сэмплов без таблиц снаружи; порог перехода на зашитые
    const acid_petri_frame_t *baked;
    int nbaked, bi;
} acid_petri_t;

// Зашитые таблицы (acid_petri_tables.c, генерирует tools/petri_bake).
extern const acid_petri_frame_t acid_petri_baked[];
extern const int acid_petri_nbaked;

void acid_petri_init(acid_petri_t *p, float sr);
// Набор зашитых таблиц: звучит сразу и после 0.5 с без таблиц снаружи. NULL - без набора (молчит до таблицы).
void acid_petri_use_baked(acid_petri_t *p, const acid_petri_frame_t *f, int n);
// Копии 2/4/8 средним по кругу.
void acid_petri_wave_build(acid_petri_wave_t *w, const int16_t tab[ACID_PETRI_TABLE]);
// Таблица снаружи в ящик (фаза событий). Последняя побеждает.
void acid_petri_post(acid_petri_t *p, const acid_petri_frame_t *f);
// Нота note с частотой hz: та же нота перезапускает свой голос, иначе свободный, иначе самый старый.
void acid_petri_note_on(acid_petri_t *p, int note, float hz, int vel);
// Зажатая нота note - на частоту hz без перезапуска (слой тональности сменил шаг).
void acid_petri_retune(acid_petri_t *p, int note, float hz);
void acid_petri_note_off(acid_petri_t *p, int note);
void acid_petri_notes_off(acid_petri_t *p);
// Голоса молчат сразу (смена типа: звук уже ушёл).
void acid_petri_kill(acid_petri_t *p);
// Атака и релиз в секундах (при смене ручки).
void acid_petri_set_env(acid_petri_t *p, float attack_s, float release_s);
// Фриз: включение копирует звучащую таблицу, переход 30 мс.
void acid_petri_freeze(acid_petri_t *p, int on);
// 1, если с прошлого вызова сеятель прыгнул (бьёт огибающую фильтра).
int acid_petri_jumped(acid_petri_t *p);
// Модуляция i звучащей таблицы, -1..1.
float acid_petri_mod(const acid_petri_t *p, int i);
// 1 - звучит хоть один голос.
int acid_petri_active(const acid_petri_t *p);
// Редкие шаги сэмпла (не инлайн): забрать ящик, поставить в ящик зашитую, пересчитать усиления голосов.
void acid_petri_take(acid_petri_t *p);
void acid_petri_queue_baked(acid_petri_t *p);
void acid_petri_gains(acid_petri_t *p);

#define ACID_PETRI_CTRL 16 // сэмплов между пересчётами усилений

// Сэмпл суммы голосов (до фильтра). Инлайн: вызов из другого файла на каждый сэмпл дорог.
static inline float acid_petri_osc(acid_petri_t *p)
{
    if (p->quiet < p->quiet_max) {
        p->quiet++;
    } else if (p->nbaked && !p->pending && p->x >= 1.0f) {
        acid_petri_queue_baked(p); // панели нет: дальше по зашитым
    }
    if (p->x < 1.0f) {
        p->x += p->dx;
        if (p->x > 1.0f) {
            p->x = 1.0f;
        }
    } else if (p->pending) {
        acid_petri_take(p);
    }
    if (p->m != p->mt) {
        p->m += (p->mt - p->m) * p->mc;
        float d = p->mt - p->m;
        if (d < 1e-4f && d > -1e-4f) {
            p->m = p->mt;
        }
    }
    if (--p->ctrl <= 0) {
        acid_petri_gains(p);
        p->ctrl = ACID_PETRI_CTRL;
    }
    const float x = p->x, m = p->m;
    const acid_petri_wave_t *A = &p->w[p->a], *B = &p->w[p->b];
    float out = 0.0f;
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        acid_petri_voice_t *v = &p->v[k];
        if (!v->on) {
            continue;
        }
        v->ph += v->inc;
        uint32_t i = v->ph >> 24;
        float fr = (float)(v->ph & 0xFFFFFFu) * (1.0f / 16777216.0f);
        const int16_t *ta = A->t[v->level], *tb = B->t[v->level];
        float sa = ta[i] + (ta[i + 1] - ta[i]) * fr, sb = tb[i] + (tb[i + 1] - tb[i]) * fr;
        float s = sa + (sb - sa) * x;
        if (m > 0.0005f) {
            const int16_t *tf = p->fz.t[v->level];
            float sf = tf[i] + (tf[i + 1] - tf[i]) * fr;
            s += (sf - s) * m;
        }
        s *= 1.0f / 32768.0f;
        float y = s - v->x1 + 0.9954f * v->y1; // без постоянной составляющей
        v->x1 = s;
        v->y1 = y;
        v->lvl += ((y < 0.0f ? -y : y) - v->lvl) * 0.000367f;
        v->env += ((float)v->gate - v->env) * (v->gate ? p->ea : p->er);
        out += y * v->g * v->env;
        if (!v->gate && v->env < 1e-4f) {
            v->on = 0;
        }
    }
    return out;
}
