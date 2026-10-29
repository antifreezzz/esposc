# Tonal Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** страница 5 - круг до 8 шагов транспозиции синтов по тактам клока BSP, в хроматике или по ладу, с сохранением во flash.

**Architecture:** состояние и логика круга в новом `acid_tonal.c` (как `acid_clock.c`), без знания о синтах.
`acid_synth.c` подаёт тики, Start/Continue и CC страницы 5, сдвигает ноты при нажатии и звучащие ноты на границе.

**Tech Stack:** C11, ESP-IDF v6.1, хост-тесты `host/test.h`.

**Spec:** `docs/superpowers/specs/2026-10-29-tonal-layer-design.md`

## Global Constraints

- Комментарии по-русски, код и git по-английски, без длинных тире. TDD: красный, потом зелёный.
- Выборы по числу на экране, вне списка - игнор. Шаги: 64 без сдвига, хроматика 52..76, лад 57..71 (в ладу клип до ±7).
- Такт = 96 тиков. Нота за 10 мс (480 сэмплов) до границы - на новый шаг мгновенно.
- Автопилот бит в бит как в `main`. В рендере и MIDI без локов, аллокаций, логов.
- `BLOB_MAGIC` поднять, ручки страницы 5 в blob.

## Review Focus

1. Нота нажата, шаг сменился, нота отпущена: не залипла, высота при отпускании верхней при зажатой нижней - по текущему шагу.
2. Ручка длины или числа шагов повёрнута посреди круга: шаг не выходит за число шагов, позиция не уходит за длину.
3. Тики без Start (плата включилась посреди игры): круг всё равно идёт.
4. Слой выключен (длина 0): live-рендер бит в бит как без слоя.
5. Импорт старого blob (старая магия) отвергается, синт стартует с умолчаний без мусора.

---

### Task 1: Tonal module

**Files:** create `components/acid/acid_tonal.c`, `components/acid/include/acid_tonal.h`, `host/test_tonal.c`;
modify `components/acid/CMakeLists.txt`, `host/CMakeLists.txt`.

**Produces:**
```c
#define ACID_TONAL_KNOBS 16 // по энкодерам, индекс = энкодер - 1, 0 не используется
typedef struct {
    uint8_t knob[ACID_TONAL_KNOBS]; // сырые значения, 0xFF - не трогали (для blob)
    int len, nsteps, order, target, held_mode, scale, tonic;
    int8_t shift[8];                // шаг в полутонах (хроматика) или ступенях (лад), уже с клипом
    int bar_len;                    // длина в тиках, защёлкнута на последней границе
    uint32_t ticks;
    int step, dir;
    uint32_t rnd;
} acid_tonal_t;
void acid_tonal_init(acid_tonal_t *t);
void acid_tonal_set(acid_tonal_t *t, int enc, int v);  // enc 1..15 (0-based индекс энкодера), игнор вне списка
void acid_tonal_start(acid_tonal_t *t);                // шаг 1, счёт с нуля
int  acid_tonal_tick(acid_tonal_t *t);                 // 1 - на этом тике граница шага
int  acid_tonal_active(const acid_tonal_t *t);         // len > 0
int  acid_tonal_step(const acid_tonal_t *t);           // 0..7
int  acid_tonal_note(const acid_tonal_t *t, int note); // нота со сдвигом текущего шага
int  acid_tonal_moves(const acid_tonal_t *t, int slot); // двигать ли синт slot (0/1)
uint32_t acid_tonal_ticks_left(const acid_tonal_t *t); // тиков до границы
```
Умолчания: len 0, nsteps 4, order 0, target 0, held_mode 1, scale 0, tonic 0, shift все 0.

- [ ] Тесты (`host/test_tonal.c`):
  - `test_tonal_defaults_inactive`: после init не активен, `note(60) == 60`.
  - `test_tonal_values_screen_exact`: enc 1 (энкодер 2) = 3 -> len 3; = 17 -> игнор; enc 2 = 0 и 9 -> игнор, 8 -> nsteps 8;
    enc 8 (шаг 1) = 76 -> +12, = 77 -> игнор; enc 6 = 8 -> игнор; enc 7 = 12 -> игнор.
  - `test_tonal_bar_boundary`: len 1, start, 95 тиков `tick()==0`, 96-й `==1` и step 1; len 2 - граница на 192-м.
  - `test_tonal_wraps_nsteps`: nsteps 3, после 3 границ step 0.
  - `test_tonal_pingpong`: nsteps 4, order 1, шаги 0 1 2 3 2 1 0 1.
  - `test_tonal_random_no_repeat`: order 2, nsteps 4, 200 границ - соседние разные, все 4 встречаются; nsteps 1 - всегда 0.
  - `test_tonal_start_resets`: посреди круга `start()` -> step 0, `ticks_left == len*96`.
  - `test_tonal_len_change_next_boundary`: len 4, 100 тиков, len 1 -> граница ещё на 384-м тике, следующая через 96.
  - `test_tonal_nsteps_shrink_clamps`: step 5 из 8, nsteps 4 -> на следующей границе step 0, `step()` до неё < 8.
  - `test_tonal_chromatic`: шаг 1 = 67 -> `note(60) == 63`.
  - `test_tonal_scale_major`: scale 1, tonic 0, шаг +2 (66): E64 -> G67, B71 -> D74 (перенос октавы), F#66 -> A#70 (F -> A плюс остаток).
  - `test_tonal_scale_down_octave`: до-мажор, шаг -7 (57): C60 -> C48; клип: шаг 52 в ладу = -7.
  - `test_tonal_scale_tonic`: ля-минор (scale 2, tonic 9), шаг +1: A57 -> B59, E64 -> F65.
  - `test_tonal_pentatonic`: scale 7, tonic 9 (A минорная пентатоника A C D E G), шаг +1: A57 -> C60, G67 -> A69.
  - `test_tonal_moves`: target 0 - оба; 1 - только slot 0; 2 - только slot 1.
- [ ] RED (файл не собирается без модуля - создать заголовок с пустыми заглушками, увидеть падение проверок), реализация, GREEN.
- [ ] Commit `feat(acid): tonal layer step cycle and scale transposition`.

Лады (полутоны от тоники): мажор 0 2 4 5 7 9 11, минор 0 2 3 5 7 8 10, дорийский 0 2 3 5 7 9 10,
фригийский 0 1 3 5 7 8 10, миксолидийский 0 2 4 5 7 9 10, гармонический минор 0 2 3 5 7 8 11,
минорная пентатоника 0 3 5 7 10. Нота вне лада: ближайшая ступень ниже плюс остаток.

### Task 2: Page 5 in live

**Files:** `acid_synth.h/.c` (поле `tonal`, `PAGES 6`, `PAGE_TONAL 5`, Start/Continue, сдвиг нот), `host/test_live.c`.

**Consumes:** Task 1.

- [ ] Тесты live (`host/test_live.c`), клок подаётся тиками 0xF8 каждые 250 сэмплов (120 BPM):
  - `test_live_tonal_page`: `cc(10,5)`, `cc(74,1)` -> `s.tonal.len == 1`; страница 4 и CC 74 - хруст, не слой.
  - `test_live_tonal_shifts_next_bar`: len 1, шаг 2 = 69 (+5); Start, нота 45 на синте 1 после 96 тиков звучит 45+5
    (`acid_synth_pitch_hz` ~146.8 Гц), до границы - 110 Гц.
  - `test_live_tonal_held_modes`: нота держится через границу: held_mode 0 - высота прежняя; 1 - `gliding` и цель новая;
    2 - `pitch_log2` сразу новый, без `retrig`.
  - `test_live_tonal_early_note`: нота за 240 сэмплов до границы, held_mode 0 - после границы высота нового шага.
  - `test_live_tonal_release_no_stuck`: нота нажата на шаге 1, отпущена на шаге 2 -> `nheld == 0`; две ноты, отпустили верхнюю
    после смены шага - цель = нижняя со сдвигом текущего шага.
  - `test_live_tonal_target`: target 2 - синт 1 без сдвига, синт 2 со сдвигом.
  - `test_live_tonal_continue`: Stop, Continue - шаг сохраняется; Start - шаг 0.
  - `test_live_tonal_off_bitexact`: два live-рендера с нотами и клоком, в одном страница 5 тронута только шагами (len 0) -
    выходы равны бит в бит.
- [ ] RED, реализация: `note_on`/`note_release`/`type_switch` считают ноту через `acid_tonal_note`, если `acid_tonal_moves`;
  на границе для звучащих нот синтов по `held_mode`, нота моложе 480 сэмплов - прыжок.
- [ ] GREEN, автопилот `cmp` с `main`, commit `feat(acid): tonal layer page 5 transposes synths on bar boundaries`.

### Task 3: Persistence

**Files:** `acid_synth.c/.h` (`BLOB_MAGIC` версия +1, `ACID_SYNTH_BLOB` + `ACID_TONAL_KNOBS`, export/import, `save = 1` при CC страницы 5),
`host/test_slot.c` или `host/test_live.c`.

- [ ] Тесты: `test_tonal_blob_roundtrip` - выставить len 2, лад 3, шаг 4 = 70, export, import в новый синт -> те же поля;
  `test_blob_old_magic_rejected` - blob с прежней магией не применяется; CC страницы 5 ставит `acid_synth_take_save`.
- [ ] RED, реализация (при import каждое сохранённое значение != 0xFF - через `acid_tonal_set`), GREEN, commit.

### Task 4: Docs and board check

- [ ] `README.md` (возможности, таблица ручек столбец «5 Тональность»), `docs/controls.html` (`PAGES[5]`, цвет, карточка
  «Тональность»), скриншот страницы.
- [ ] Прошить, лог: `worst render` без роста, нет underrun (без BSP - просто старт и 30 с лога).
- [ ] Commit docs.
