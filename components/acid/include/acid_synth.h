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
#include "acid_seq.h"

// Acid-трек: бас-голос, ударные и аранжировка на 64 такта, либо live-режим от MIDI.
// Поля публичные ради тестов, снаружи трогать только res, wave и kick_on (в режиме фиксированного паттерна).
typedef struct {
    float sr;
    acid_osc_t osc;
    acid_ladder_t flt;
    acid_decay_t fenv;
    acid_amp_t amp;
    acid_seq_t seq;
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
    uint8_t last_flags; // флаги последнего сыгранного шага (для слайда через такт)
    acid_kick_t kick;
    acid_hat_t hat;
    acid_clap_t clap;
    int kick_on;       // прямая бочка (только в режиме фиксированного паттерна)

    // Аранжировка.
    int arr_on;
    int started;
    int bar;           // 0..63
    int cycle;         // номер круга
    const acid_section_t *sec;
    acid_step_t bar_steps[16];
    acid_step_t mut[ACID_PAT_COUNT][16]; // паттерны текущего круга (с мутацией)

    // Параметры звука: в live-режиме их крутят ручки, иначе стартовые значения.
    float cut_base;    // базовый срез, Гц (в режиме трека его задаёт автоматизация)
    float env_hz;      // глубина огибающей фильтра обычной ноты, Гц
    float decay_s;     // спад огибающей фильтра обычной ноты
    float accent_amt;  // усиление громкости акцента
    float bass_gain, kick_gain, hat_gain, clap_gain;
    float drive, master;
    float kick_vel, hat_vel, clap_vel; // velocity последнего удара, 0..1

    // Live-режим: ноты от MIDI, свой секвенсор молчит.
    int live;
    int8_t held[8];    // удерживаемые ноты баса, последняя сверху
    uint32_t held_at[8]; // когда нажата (s->now)
    int nheld;
    uint32_t now;      // сэмплов с init
    int pend_note;     // короткая нота с отложенным отпусканием (дребезг пэда), -1 если нет
    uint32_t pend_at;  // когда отпустить
} acid_synth_t;

// Режим трека по умолчанию.
void acid_synth_init(acid_synth_t *s, float sr);
// Фиксированный паттерн без аранжировки (для тестов голоса).
void acid_synth_set_pattern(acid_synth_t *s, const acid_step_t *steps, int len);
void acid_synth_render(acid_synth_t *s, float *out, size_t n);
float acid_synth_pitch_hz(const acid_synth_t *s);
// 1: live-режим, ждём MIDI; 0: автопилот с начала трека. Голоса не обрываются.
void acid_synth_set_live(acid_synth_t *s, int on);
// Событие MIDI в live-режиме (вне его игнорируется): бас канал 5, барабаны канал 2, CC с любого.
void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev);
