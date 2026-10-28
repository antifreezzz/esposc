# Plaits Synths Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** два независимых синта на SEQ1/SEQ2, у каждого тип 303 или одна из 24 моделей Plaits, с памятью ручек.

**Architecture:** Plaits (C++) за C-обёрткой в `components/plaits`, слоты синтов в `acid_synth`,
слот 2 на ядре 0 в прошивке, NVS для памяти ручек.

**Tech Stack:** C11 + C++17 (Plaits), ESP-IDF v6.1, host-тесты CMake/ctest.

**Spec:** `docs/superpowers/specs/2026-10-28-plaits-synths-design.md`

## Global Constraints

- Комментарии и доки на русском, код и git на английском, без длинных тире.
- TDD. Хост-тесты: `cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build`.
- Автопилот бит в бит как в `main` (сравнение 130 с рендера через `cmp`).
- В рендере никаких локов и аллокаций.

## Review Focus

1. Ретриггер Plaits: note off и note on в одном куске MIDI дают новую атаку (`test_plaits_slot_retrigger`).
2. Смена типа во время звучащей ноты без скачка (`test_slot_type_switch_no_click`).
3. Память ручек: тип A -> B -> A возвращает ручки A, умолчания B не задеты чужими ручками (`test_slot_knob_memory`).
4. Стоп и отключение BSP глушат оба синта (`test_live_stop_silences_both`).
5. Синт 2 в отдельном буфере (как на ядре 0) звучит так же, как в одном потоке (`test_render_split_identical`).

---

### Task 1: Plaits port and C wrapper

**Files:** Create `components/plaits/` (копия `plaits/dsp`, `plaits/resources.*`, `plaits/user_data.h`,
`stmlib/stmlib.h`, `stmlib/dsp`, `stmlib/utils`), `components/plaits/acid_plaits.cc`,
`components/plaits/include/acid_plaits.h`, `components/plaits/CMakeLists.txt`, `host/test_plaits.c`;
modify `host/CMakeLists.txt` (C++).

**Interfaces - Produces:**
```c
#define ACID_PLAITS_MODELS 24
typedef struct {
    int model;            // 0..23
    float note;           // MIDI-нота, дробная (глайд)
    float harmonics, timbre, morph, decay, lpg_colour; // 0..1
    float fm_env, timbre_env, morph_env; // -1..1
    float level;          // 0..1, уровень гейта
    int trigger;          // 0/1
} acid_plaits_params_t;
typedef struct acid_plaits acid_plaits_t;
acid_plaits_t *acid_plaits_create(void); // один раз при старте
void acid_plaits_render(acid_plaits_t *p, const acid_plaits_params_t *pp, float *out, int n); // n == 12
```

- [ ] Тесты: все 24 модели после триггера дают конечный звук с RMS > 0.01; модель 8 (VA) нотой 45 -
  110 Гц по переходам через ноль; гейт снят - через 1 с RMS < 1e-3 (кроме моделей с собственной
  огибающей, они и так затухают); `test_plaits_dx7_retrigger_smooth`: модель 2, фраза с ретриггером
  звучащего голоса, вторая разность < 3000 (до правки ~10000).
- [ ] RED, правка DX7 и обёртка, GREEN, commit `feat(plaits): vendor Plaits DSP behind a C wrapper`.

### Task 2: Synth slots and pages

**Files:** Modify `acid_synth.{h,c}`, `host/test_live.c`, `host/test_synth.c`, `host/test_track.c`.

**Interfaces - Produces:**
```c
typedef struct { ... } acid_slot_t;   // голос 303, голос Plaits, стек нот, тип, ручки
// в acid_synth_t: acid_slot_t slot[2]; 
void acid_synth_render_slot(acid_synth_t *s, int k, float *out, size_t n); // сырой выход слота k
void acid_synth_render_with(acid_synth_t *s, float *out, size_t n, const float *slot1); // всё, слот 1 из буфера
```
`acid_synth_render` = `render_slot(1)` + `render_with`. Канал 5 -> слот 0, канал 6 -> слот 1. Страниц 4.

- [ ] Тесты: существующие live-тесты переведены на `s.slot[0]`, раскладка микса и страниц по спеке;
  `test_live_seq2_plays_slot1`; `test_live_stop_silences_both`; `test_render_split_identical`;
  автопилот бит в бит.
- [ ] RED, реализация, GREEN, commit `feat(acid): two synth slots on SEQ1 and SEQ2, four pages`.

### Task 3: Synthesis type per slot

**Files:** Modify `acid_synth.{h,c}`; create `host/test_slot.c`.

- [ ] Тесты: `test_slot_type_select` (энкодер 2: 0 -> 303, 127 -> модель 23); `test_slot_plaits_plays`
  (тип FM, нота 45 звучит); `test_plaits_slot_retrigger`; `test_plaits_slot_slide` (перекрытие без
  нового триггера, высота ползёт); `test_slot_type_switch_no_click`; `test_slot_knob_memory`;
  `test_slot_plaits_page` (энкодеры 3-13 по спеке); `test_slot_dirty_save` (флаг сохранения
  поднимается только при смене типа после изменений) и `acid_synth_export/import` blob туда-обратно.
- [ ] RED, реализация, GREEN, commit `feat(acid): pick 303 or a Plaits model per synth, knob memory`.

### Task 4: Firmware

**Files:** Modify `main/main.c`, `main/CMakeLists.txt`, `sdkconfig.defaults`; create `main/store.{c,h}`.

- [ ] 240 МГц; задача слота 1 на ядре 0 с синхронизацией по кускам; NVS: загрузка при старте,
  запись после смены типа при поднятом флаге.
- [ ] Прошивка, лог: worst render по ядрам, underrun'ы, время записи NVS. Commit `feat: second synth on core 0, knob memory in NVS`.
