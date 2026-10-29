# Pump and DJ Filter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** пампинг от удара бочки и DJ-фильтр на мастере, ручки на странице 0 (энкодеры 10-13).

**Architecture:** чистый DSP в новом модуле `acid_fx.c` (пампинг и фильтр, без знания о синте),
встраивание в микс `acid_synth_render_with`, ручки в `page_mix`, удар в `drum_on`.
Нейтральные ручки - старый путь микса без изменений, отсюда бит в бит.

**Tech Stack:** C11, ESP-IDF v6.1, хост-тесты CMake/ctest (`host/test.h`).

**Spec:** `docs/superpowers/specs/2026-10-29-pump-djfilter-design.md`

## Global Constraints

- Комментарии и доки на русском, код и git на английском, без длинных тире.
- TDD: тест до кода, увидеть красный. `cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build`.
- Автопилот бит в бит как в `main` (рецепт с `git worktree` и `cmp` в `CLAUDE.md`).
- В рендере и в обработке CC никаких локов, аллокаций и логов. `tanf`/`expf` в обработке CC допустимы.
- `BLOB_MAGIC` не трогать.
- Ручки: энкодер 10 (CC 18) глубина 0..1, 11 (CC 19) возврат 30..600 мс экспоненциально,
  12 (CC 16) фильтр, центр 64, мёртвая зона 62..66, 13 (CC 17) резонанс Q 0.7..8.
- ФНЧ 0..61: ~18 кГц у 61 вниз до ~150 Гц у 0. ФВЧ 67..127: ~20 Гц у 67 вверх до ~3 кГц у 127. Оба экспоненциально.
- Q у края мёртвой зоны 0.7, на 12 щелчков от края полный Q с энкодера 13.
- Атака пампинга ~3 мс. Возврат: постоянная времени `tau` = значение энкодера 11 (*решение плана*).
- Порядок: `бочка + пампинг * (синты + хэты + клэп + малый)` -> фильтр -> `tanh` drive -> master.

## Review Focus

1. Быстрый поворот фильтра через центр одним CC (BSP пропускает значения: 58 -> 70) при высоком резонансе:
   ни щелчка, ни выброса. Тест `test_djf_cross_centre_no_click` (Task 1).
2. Глубину пампинга сняли в 0 посреди приседа: громкость возвращается плавно, а не прыжком.
   Тест `test_pump_depth_off_mid_dip_smooth` (Task 1).
3. Ручки покрутили и вернули в нейтраль: звук снова бит в бит как без них.
   Тест `test_live_fx_back_to_neutral_bitexact` (Task 2).
4. Бочка выведена в 0: пампинг всё равно качает (решение пользователя). Тест `test_live_pump_ghost_kick` (Task 2).
5. Высокий резонанс у ФВЧ на низком срезе под бочкой: выход ограничен `tanh`, без NaN и Inf.
   Тест `test_live_djf_resonance_bounded` (Task 2).

---

### Task 1: DSP module `acid_fx` (pump + DJ filter)

**Files:**
- Create: `components/acid/acid_fx.c`, `components/acid/include/acid_fx.h`, `host/test_fx.c`
- Modify: `components/acid/CMakeLists.txt` (добавить `"acid_fx.c"` в SRCS; хост берёт исходники глобом сам)

**Interfaces - Produces:**
```c
// Пампинг: огибающая e 0..1, множитель громкости 1 - depth * e.
typedef struct {
    float sr;
    float e, target;      // огибающая и цель атаки (velocity удара)
    float atk_coef, rel_coef;
    float depth, depth_s; // глубина с ручки и сглаженная
} acid_pump_t;
void acid_pump_init(acid_pump_t *p, float sr);              // depth 0, tau 0.15 с
void acid_pump_set_depth(acid_pump_t *p, float depth);      // 0..1
void acid_pump_set_release(acid_pump_t *p, float tau_s);    // постоянная времени
void acid_pump_hit(acid_pump_t *p, float vel);              // vel 0..1, цель max(target, vel)
float acid_pump_next(acid_pump_t *p);                       // множитель, сэмпл
int acid_pump_active(const acid_pump_t *p);                 // 0: depth и depth_s == 0, можно не звать next

// DJ-фильтр: TPT SVF, ФНЧ или ФВЧ, обход в центре с кроссфейдом.
typedef struct {
    float sr;
    float ic1, ic2;
    float g, k, g_target, k_target; // сглаживаются к цели посэмплово или кусками
    int mode, next_mode;            // 0 обход, 1 ФНЧ, 2 ФВЧ
    float wet;                      // 0..1, доля фильтра в кроссфейде
    int knob, res_knob;
} acid_djf_t;
void acid_djf_init(acid_djf_t *f, float sr);                // knob 64, res 0: обход
void acid_djf_set(acid_djf_t *f, int knob, int res_knob);   // из обработки CC
float acid_djf_next(acid_djf_t *f, float in);
int acid_djf_active(const acid_djf_t *f);                   // 0: обход и кроссфейд закончен
float acid_djf_cutoff_hz(int knob);                         // 0 в мёртвой зоне
float acid_djf_q(int knob, int res_knob);
```

Смена режима (ФНЧ <-> ФВЧ, вход в обход и выход из него) идёт через `wet`: увести к 0 за ~5 мс,
сменить `mode`, сбросить `ic1/ic2`, вернуть к 1. Пока `wet` < 1, выход `in + wet * (filtered - in)`.

- [ ] **Step 1: Тесты в `host/test_fx.c`** (свой `main` с `RUN`, как в других `test_*.c`)

Пампинг (SR 48000):
- `test_pump_neutral`: init, hit(1) -> `acid_pump_active == 0`, 48000 вызовов `next` дают ровно `1.0f`.
- `test_pump_dips_and_recovers`: depth 1, tau 0.1, hit(1): минимум за первые 20 мс < 0.05; после 0.6 с > 0.99.
- `test_pump_attack_not_instant`: depth 1, hit(1): первый `next` > 0.9; наибольший шаг между соседними < 0.02.
- `test_pump_velocity`: depth 1, hit(0.5): минимум за 20 мс в 0.5 +/- 0.05.
- `test_pump_soft_hit_keeps_dip`: depth 1, tau 0.2, hit(1), 20 мс, затем hit(0.3): следующий `next`
  не выше предыдущего больше чем на 0.001, и ещё 20 мс множитель не поднимается выше, чем без второго удара.
- `test_pump_depth_off_mid_dip_smooth`: depth 1, hit(1), 10 мс, depth 0: наибольший шаг < 0.02,
  через 100 мс `acid_pump_active == 0`, `next` ровно `1.0f`.

Фильтр:
- `test_djf_map`: `cutoff_hz(62..66) == 0`; `cutoff_hz(61)` 18000 +/- 1500; `cutoff_hz(0)` 150 +/- 15;
  `cutoff_hz(67)` 20 +/- 3; `cutoff_hz(127)` 3000 +/- 300; 0..61 строго растёт, 67..127 строго растёт.
- `test_djf_q_ramp`: `q(61,127)` 0.7 +/- 0.01; `q(49,127)` 8 +/- 0.1; `q(0,127)` 8 +/- 0.1; `q(0,0)` 0.7 +/- 0.01;
  `q(67,127)` 0.7 +/- 0.01; `q(79,127)` 8 +/- 0.1.
- `test_djf_bypass_bitexact`: init, `set(64,127)`: `next(x) == x` для шума 48000 сэмплов; `active == 0`.
- `test_djf_lp_kills_highs`: `set(0,0)`, синус 5 кГц амплитуда 0.5, RMS последних 0.5 с из 1 с < 0.03 от входного;
  синус 50 Гц проходит с RMS > 0.6 от входного.
- `test_djf_hp_kills_lows`: `set(127,0)`, синус 50 Гц < 0.03 от входного; синус 10 кГц > 0.6.
- `test_djf_knob_step_no_zipper`: синус 1 кГц амплитуда 0.5, `set(30,64)`, 0.2 с, `set(31,64)`:
  наибольшая разность соседних сэмплов за 50 мс после смены < 0.12 (у самого синуса ~0.065).
- `test_djf_cross_centre_no_click`: синус 200 Гц амплитуда 0.5, `set(58,127)`, 0.2 с, `set(70,127)`:
  наибольшая разность соседних < 0.1, все сэмплы конечны, через 50 мс `mode == 2`.

- [ ] **Step 2: RED.** `cmake --build host/build` падает на отсутствии `acid_fx.h`, затем со заглушками - тесты FAIL.
- [ ] **Step 3: Реализация `acid_fx.c`.** SVF как в `acid_svf_bp_hp` (`acid_noise.c`, его не менять):
  `lp = v2`, `hp = in - k*v1 - v2`, `k = 1/Q`, `g = tanf(pi*fc/sr)`. `tanf` только в `acid_djf_set`,
  в `next` сглаживать `g` и `k` к цели одним полюсом (~5 мс).
- [ ] **Step 4: GREEN.** `ctest --test-dir host/build` - все наборы ok.
- [ ] **Step 5: Commit** `feat(acid): kick pump envelope and DJ filter DSP`.

### Task 2: Wire into the mix, knobs and kick

**Files:**
- Modify: `components/acid/include/acid_synth.h` (поля `acid_pump_t pump; acid_djf_t djf; int djf_knob, djf_res;`)
- Modify: `components/acid/acid_synth.c`: `acid_synth_init` (init обоих), `page_mix` (case 9-12),
  `drum_on` (нота 36 -> `acid_pump_hit(&s->pump, v)`), `acid_synth_render_with` (микс)
- Test: `host/test_live.c`

**Interfaces - Consumes:** всё из Task 1.

`page_mix`: case 9 `acid_pump_set_depth(lin(v,0,1))`, case 10 `acid_pump_set_release(expo(v,0.03,0.6))`,
case 11 и 12 запоминают `djf_knob`/`djf_res` и зовут `acid_djf_set`. Начальные `djf_knob = 64`, `djf_res = 0`.

Микс: если `!acid_pump_active && !acid_djf_active` - нынешнее выражение без единого изменения порядка сложения.
Иначе `y = kick + pump * (slot0 + hats + clap + snare + slot1)`, затем `y = acid_djf_next(y)`, затем `tanh` и master.
Пампинг: `acid_pump_next` зовётся каждый сэмпл, пока активен, даже без бочки в миксе.

- [ ] **Step 1: Тесты в `host/test_live.c`** (страница 0: `cc(10, 0)`; энкодеры 10-13 = CC 18, 19, 16, 17)
- `test_live_pump_ghost_kick`: бочка в нуле (`cc(77,0)`), синт 1 держит ноту (303), `cc(18,127)`, `cc(19,0)`;
  RMS 20 мс до удара `r0`; `on(DRUMS,36,127)`; RMS окна 5-15 мс после удара < 0.3 * r0; через 0.5 с RMS > 0.9 * r0.
  Бочка в нуле заодно мерит только синт.
- `test_live_pump_kick_not_ducked`: только бочка, depth 127 против depth 0 (два экземпляра синта):
  RMS первых 100 мс совпадают в пределах 1%.
- `test_live_pump_depth0_no_effect`: глубина 0, удар бочки при звучащем синте: буфер бит в бит как
  у второго экземпляра, где ручку глубины не трогали.
- `test_live_djf_cuts_highs`: закрытый хэт по кругу, `cc(16,0)` против нейтрали: RMS < 0.2 от нейтрального.
- `test_live_djf_resonance_bounded`: `cc(16,70)`, `cc(17,127)`, бочки по 120 BPM 2 с: все сэмплы конечны,
  `|y| <= master * OUT_PEAK` (через `tanh`).
- `test_live_fx_back_to_neutral_bitexact`: два экземпляра `s`, `s2` (сделать `s2` статическим, вспомогательные
  функции принимают указатель). `s2`: `cc(18,127)`, `cc(16,20)`, 0.1 с тишины, `cc(18,0)`, `cc(16,64)`, 0.2 с тишины.
  Затем оба получают одинаковые ноты синта 1, бочку и хэт и рендерят 1 с: буферы равны `memcmp`.
- [ ] **Step 2: RED.** Новые тесты FAIL (CC 18/19/16/17 сейчас никуда не идут).
- [ ] **Step 3: Реализация** по описанию выше.
- [ ] **Step 4: GREEN.** Все наборы ok.
- [ ] **Step 5: Автопилот бит в бит.** Рецепт из `CLAUDE.md`, `cmp /tmp/a.wav /tmp/b.wav` без вывода.
- [ ] **Step 6: Commit** `feat(acid): kick pump and master DJ filter on mix page encoders 10-13`.

### Task 3: Docs and hardware budget

**Files:**
- Modify: `README.md` (таблица ручек, колонка «0 Микс»: 10 пампинг глубина, 11 пампинг возврат, 12 DJ-фильтр,
  13 резонанс; абзац в «Что умеет» про пампинг от ноты бочки, в том числе при бочке в нуле, и про фильтр с центром 64)
- Modify: `docs/controls.html` (`PAGES[0]`: энкодеры 10-13 и тексты карточки страницы микса)

- [ ] **Step 1: Обновить доки.** Проверка: открыть `docs/controls.html` в snap-chromium headless
  (`--screenshot=$HOME/x.png`), страница 0 показывает четыре новые ручки.
- [ ] **Step 2: Прошивка.** `. ~/esp/esp-idf-v6.1/export.sh && idf.py -p /dev/ttyACM0 flash`, сборка без ошибок.
- [ ] **Step 3: Бюджет на железе (с пользователем).** Попросить пользователя: оба синта на тип 21 (modal),
  глубина пампинга 127, фильтр на 30, резонанс 127, играть 30 с. Лог через pyserial в файл:
  `worst render` < 2000 мкс, без WARN underrun. Записать цифру в спеку (раздел «Проверка»).
- [ ] **Step 4: Commit** `docs: stage 6 knobs in README and controls guide`.
