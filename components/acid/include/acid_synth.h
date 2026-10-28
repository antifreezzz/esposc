#pragma once
#include <stddef.h>
#include <stdint.h>

#include "acid_arr.h"
#include "acid_drums.h"
#include "acid_env.h"
#include "acid_filter.h"
#include "acid_kick.h"
#include "acid_midi.h"
#include "acid_osc.h"
#include "acid_plaits.h"
#include "acid_seq.h"

#define ACID_TYPES (1 + ACID_PLAITS_MODELS) // 0 - наш 303, 1..24 - модели Plaits
#define ACID_KNOBS 16

// Синт-слот: тип синтеза, голоса 303 и Plaits, ручки по типам, стек нот.
// Слот 0 играет автопилот и SEQ1, слот 1 - SEQ2.
typedef struct {
    int type;          // звучащий тип
    int sel_type;      // выбор с BSP; автопилот играет 303, выбор ждёт возвращения BSP
    int next_type;     // куда переходим после ухода звука
    int fade;          // смена типа: > 0 уход звука, < 0 приход, сэмплов осталось
    uint8_t knob[ACID_TYPES][ACID_KNOBS]; // сырые CC энкодеров по типам, 0xFF - не трогали

    acid_osc_t osc;
    acid_ladder_t flt;
    acid_decay_t fenv;
    acid_amp_t amp;
    float pitch_log2;  // текущая высота, log2(Гц)
    float target_log2;
    float glide_coef;
    int gliding;
    float accent_gain;
    float fenv_amount; // Гц на пике огибающей фильтра
    float res;
    int wave;          // 0 пила, 1 меандр
    float g;           // коэффициент фильтра с управляющей частоты
    int ctrl_left;     // сэмплов до следующего пересчёта
    // Ручки: в live-режиме их крутят энкодеры, иначе стартовые значения.
    float cut_base;    // базовый срез, Гц (в режиме трека его задаёт автоматизация)
    float env_hz;      // глубина огибающей фильтра обычной ноты, Гц
    float decay_s;     // спад огибающей фильтра обычной ноты
    float accent_amt;  // усиление громкости акцента
    float gain;        // громкость в миксе
    int transpose;     // полутоны, live
    // Live: ноты от MIDI.
    int8_t held[8];    // удерживаемые ноты, последняя сверху
    uint32_t held_at[8]; // когда нажата (now)
    int nheld;
    uint32_t now;      // сэмплов с init
    int pend_note;     // короткая нота с отложенным отпусканием (дребезг пэда), -1 если нет
    uint32_t pend_at;  // когда отпустить
    // Plaits.
    acid_plaits_t *plaits;
    acid_plaits_params_t pp;
    float pbuf[ACID_PLAITS_BLOCK];
    int ppos;          // позиция в pbuf
    int retrig;        // новая нота: опустить триггер на один блок
    int accented;      // текущая нота с акцентом
    float p_accent;    // насколько обычная нота тише акцентной, 0..0.75
    int idle;          // блоков тишины подряд: молчащий Plaits не считаем
} acid_slot_t;

#define ACID_SLOTS 2

// Acid-трек: бас-голос, ударные и аранжировка на 64 такта, либо live-режим от MIDI.
// Поля публичные ради тестов, снаружи трогать только slot[0].res, slot[0].wave и kick_on
// (в режиме фиксированного паттерна).
typedef struct {
    float sr;
    acid_slot_t slot[ACID_SLOTS];
    acid_seq_t seq;
    uint8_t last_flags; // флаги последнего сыгранного шага (для слайда через такт)
    acid_kick_t kick;
    acid_hat_t hat;
    acid_clap_t clap;
    acid_snare_t snare;
    int kick_on;       // прямая бочка (только в режиме фиксированного паттерна)

    // Аранжировка.
    int arr_on;
    int started;
    int bar;           // 0..63
    int cycle;         // номер круга
    const acid_section_t *sec;
    acid_step_t bar_steps[16];
    acid_step_t mut[ACID_PAT_COUNT][16]; // паттерны текущего круга (с мутацией)

    // Микс: в live-режиме его крутят ручки, иначе стартовые значения.
    float kick_gain, hat_gain, clap_gain, snare_gain;
    float drive, master;
    float kick_vel, hat_vel, clap_vel, snare_vel; // velocity последнего удара, 0..1
    // Звук ударки (ручки страницы 1): сеттеры голосов берут все параметры сразу.
    float kick_tune, kick_sweep, kick_pitch_s, kick_amp_s;
    float hat_tone, hat_closed_s, hat_open_s;
    float clap_tone, clap_tail_s;
    float snare_tune, snare_snappy, snare_decay_s;
    int page;          // страница ручек: 0 микс, 1 ударка, 2 синт 1, 3 синт 2

    int live;          // live-режим: ноты от MIDI, свой секвенсор молчит
    int save;          // сменили тип: память ручек пора записать
} acid_synth_t;

// Память ручек для flash: метка, типы и сырые ручки обоих синтов.
#define ACID_SYNTH_BLOB (4 + ACID_SLOTS * (1 + ACID_TYPES * ACID_KNOBS))

// Режим трека по умолчанию.
void acid_synth_init(acid_synth_t *s, float sr);
// Фиксированный паттерн без аранжировки (для тестов голоса).
void acid_synth_set_pattern(acid_synth_t *s, const acid_step_t *steps, int len);
void acid_synth_render(acid_synth_t *s, float *out, size_t n);
// Рендер по частям (слот 1 на другом ядре): сырой выход слота k (сейчас только k = 1),
// затем всё остальное с готовым слотом 1 из буфера. Вместе звучат как acid_synth_render.
void acid_synth_render_slot(acid_synth_t *s, int k, float *out, size_t n);
void acid_synth_render_with(acid_synth_t *s, float *out, size_t n, const float *slot1);
float acid_synth_pitch_hz(const acid_synth_t *s); // высота слота 0
// 1 и сброс флага, если с прошлого вызова сменили тип синтеза (пора записать память во flash).
int acid_synth_take_save(acid_synth_t *s);
// Память ручек в blob размера ACID_SYNTH_BLOB и обратно. import: 1 - применено, 0 - чужой blob.
size_t acid_synth_export(const acid_synth_t *s, void *buf, size_t cap);
int acid_synth_import(acid_synth_t *s, const void *buf, size_t len);
// s перед первым init должен быть обнулён (static): init создаёт голоса Plaits один раз.
// 1: live-режим, ждём MIDI; 0: автопилот с начала трека. Голоса не обрываются.
void acid_synth_set_live(acid_synth_t *s, int on);
// Событие MIDI в live-режиме (вне его игнорируется): синт 1 канал 5, синт 2 канал 6, барабаны канал 2,
// CC с любого.
void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev);
