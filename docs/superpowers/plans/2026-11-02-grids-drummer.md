# Автобарабаны Grids и барабанщик: план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** синт сам играет бочку, малый, хэт/райд, сбивки и крэш по карте Grids в темпе MIDI clock BSP, с ограничением «4 руки», управление с отдельного экрана панели.

**Architecture:** три чистых модуля без зависимостей от синта: генератор Grids (`pl_grids`, общий с панелью: она сама считает превью), счётчик шагов (`acid_beat`, задел секвенсора), барабанщик (`acid_drummer`: решения Grids -> ноты по конечностям). Синт зовёт их на тике клока (не в рендере) и шлёт ноты в свой `drum_on()`. Ручки - новая внутренняя страница 12 «Grids», обычная для сцен, своих страниц и зеркала панели.

**Tech Stack:** C11, ESP-IDF 6.1, LVGL (панель), хост-тесты на `host/test.h`, CMake.

**Spec:** `docs/superpowers/specs/2026-11-02-grids-drummer-design.md`

## Global Constraints

- TDD: хост-тест до кода, увидеть красный, потом зелёный. Тесты `host/test_*.c` (CMake их глобит сам).
- Коммит каждой задачи, git-личность `antifreezzz <me@kvatech.ru>`, сообщения по-английски, в конце `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Комментарии и доки по-русски, без длинных тире.
- В рендере (всё из `acid_synth_render*`) никаких локов, аллокаций, логов. Grids и барабанщик зовутся только из `acid_synth_midi` (тик клока).
- `BLOB_MAGIC` синта не трогать. Blob сцен - перенос старых версий без потерь.
- Ручки в спеке пронумерованы «Энк 1..12»: это индексы `enc` 1..12 в коде (энкодеры BSP 2..13; `enc` 0 - выбор страницы).
- Список ручки: значение N = пункт N, вне списка ручка не меняется.
- Лицензия: Grids GPLv3, © Emilie Gillet; прошивка становится GPLv3.
- Перед каждой правкой файлов, которые не свои, - `git diff` чистый; мутации в тестах откатывать точечно, не `git checkout <файл>`.

## Review Focus

1. Клок идёт без Start (синт перезагрузили посреди игры BSP) - автобарабаны играют, шаг считается от первого тика; после Stop молчат до Start или Continue. Тест в Task 2.
2. Grids включают посреди такта - первый удар на ближайшем шаге, без пачки догоняющих нот. Тест в Task 4.
3. «Сбивка сейчас» нажата в последней доле такта или во время идущей сбивки - сбивка не дублируется и не залезает в следующий такт дальше, чем в спеке. Тест в Task 3.
4. Сцена, записанная до этапа (blob v2), вызывается - Grids не трогается, остальные ручки как были. Тест в Task 5.
5. Энкодер 1 BSP на любом значении не открывает страницу Grids; `acid_page_to_bsp(12)` = -1, слои по-прежнему 16-17. Тест в Task 4.

---

### Task 1: Генератор Grids (порт) и лицензия

**Files:**
- Create: `components/panel_link/include/pl_grids.h`, `components/panel_link/pl_grids.c`, `components/panel_link/pl_grids_map.c`
- Create: `LICENSE` (полный текст GPLv3)
- Test: `host/test_grids.c`
- Modify: `components/panel_link/CMakeLists.txt` (если исходники перечислены явно)

**Interfaces:**
- Produces:
```c
#define PL_GRIDS_STEPS 32 // два такта шестнадцатых
#define PL_GRIDS_PARTS 3  // 0 бочка, 1 малый, 2 хэт
#define PL_GRIDS_ACCENT 192
uint8_t pl_grids_u8(int v);  // ручка 0..127 -> 0..255: v << 1 | v >> 6
uint8_t pl_grids_level(uint8_t x, uint8_t y, int part, int step); // ReadDrumMap
// Биты 0-2 - удар дорожки, 3-5 - акцент. density и perturb по дорожкам, 0..255.
uint8_t pl_grids_step(uint8_t x, uint8_t y, const uint8_t density[3], const uint8_t perturb[3], int step);
// Превью по ручкам страницы Grids (индексы enc: 2 X, 3 Y, 4-6 плотности), без хаоса: маска на 32 шага.
void pl_grids_preview(const uint8_t knob[16], uint8_t out[PL_GRIDS_STEPS]);
```

- [ ] **Step 1:** Скачать `grids/resources.cc` (узлы `node_0..node_24`, по 96 байт: дорожка * 32 + шаг) и `grids/pattern_generator.cc` из https://github.com/pichenettes/eurorack (master). Узлы и таблицу `drum_map[5][5]` перенести в `pl_grids_map.c` байт в байт, с шапкой GPLv3 и копирайтом оригинала.
- [ ] **Step 2: тесты** `host/test_grids.c`:
  - `test_corner_nodes`: `pl_grids_level(0,0,p,s)` = байт узла `drum_map[0][0]`; `(64,0)` = узел `drum_map[1][0]`; `(0,64)` = `drum_map[0][1]` (для p 0..2, s 0, 5, 31).
  - `test_mix_midpoint`: `(32,0,p,s)` = `U8Mix(a, b, 128)` для узлов `[0][0]`, `[1][0]`, где `U8Mix(a,b,w) = (a * (255 - w) + b * w) / 255` (как в avrlib).
  - `test_density_zero_silent`: плотности 0, возмущение 0 - маска 0 на всех 32 шагах в 5 точках карты.
  - `test_density_full`: плотность 255 - удар там и только там, где уровень > 0.
  - `test_accent`: бит акцента ровно при уровне > 192 и ударе.
  - `test_perturb_clamps`: уровень 250, возмущение 100 - уровень 255, удар при любой плотности > 0.
  - `test_u8`: `pl_grids_u8(0)=0, (64)=129, (127)=255`.
  - `test_preview_matches_step`: превью = `pl_grids_step` с теми же X/Y/плотностями и нулевым возмущением.
- [ ] **Step 3:** `ctest --test-dir host/build -R test_grids` - FAIL (нет символов).
- [ ] **Step 4:** Реализовать по `ReadDrumMap` и `EvaluateDrums` оригинала: узел `i = x >> 6, j = y >> 6`, вес `x << 2`, `y << 2` (uint8), удар `level > 255 - density`, акцент `level > 192`, возмущение складывается с насыщением до 255.
- [ ] **Step 5:** Все хост-тесты зелёные (`cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build`).
- [ ] **Step 6:** `LICENSE` с текстом GPLv3. Коммит `feat: port grids drum map generator (GPLv3)`.

### Task 2: Счётчик шагов от MIDI clock

**Files:**
- Create: `components/acid/include/acid_beat.h`, `components/acid/acid_beat.c`
- Test: `host/test_beat.c`

**Interfaces:**
- Produces:
```c
#define ACID_BEAT_TICKS 6  // тиков на шестнадцатую
#define ACID_BEAT_STEPS 16 // шагов в такте
typedef struct { int stopped, tick, step; uint32_t bar; } acid_beat_t;
void acid_beat_init(acid_beat_t *b);   // не остановлен: клок без Start тоже играет
void acid_beat_start(acid_beat_t *b);  // Start: следующий тик - шаг 0 такта 0
void acid_beat_continue(acid_beat_t *b); // играть дальше с текущего места
void acid_beat_stop(acid_beat_t *b);
// Тик клока. 1 - с этого тика начинается шаг b->step (0..15) такта b->bar.
int acid_beat_tick(acid_beat_t *b);
```
Соглашение как у тональности (`acid_tonal_start`): первый тик после Start - начало такта.

- [ ] **Step 1: тесты** `host/test_beat.c`:
  - `test_start_first_tick_is_step0`: start, тик -> 1, step 0, bar 0.
  - `test_step_every_6_ticks`: после start 96 тиков - ровно 16 шагов 0..15, тик 97 - step 0, bar 1.
  - `test_stop_silent`: stop - тики возвращают 0; continue - шаги идут дальше с места; start - снова шаг 0.
  - `test_no_start_plays`: init без start, тики - шаги идут с первого тика (Review Focus 1).
- [ ] **Step 2:** FAIL. **Step 3:** реализовать. **Step 4:** все хост-тесты PASS.
- [ ] **Step 5:** Коммит `feat: beat counter from MIDI clock`.

### Task 3: Барабанщик

**Files:**
- Create: `components/acid/include/acid_drummer.h`, `components/acid/acid_drummer.c`
- Test: `host/test_drummer.c`

**Interfaces:**
- Consumes: `pl_grids_step`, `pl_grids_u8` (Task 1).
- Produces:
```c
enum { DRM_ON = 1, DRM_X, DRM_Y, DRM_KICK, DRM_SNARE, DRM_HAT, DRM_CHAOS, DRM_HAT_MODE, DRM_TIME,
       DRM_FILL_EVERY, DRM_FILL_LEN, DRM_FILL_NOW }; // индексы enc страницы Grids
enum { DRM_HAT_CLOSED, DRM_HAT_OPEN, DRM_HAT_MIX };
enum { DRM_TIME_HAT, DRM_TIME_RIDE };
#define ACID_DRUMMER_MAX 4 // нот на шаг: 4 конечности
typedef struct { uint8_t note, vel; } acid_hit_t;
typedef struct {
    uint8_t knob[16];      // ручки страницы Grids (сырые, как пришли)
    int fill_from, fill_to; // шаги сбивки в текущем такте [from, to), from == to - нет
    int fill_pending;       // «сейчас» нажата: начать с ближайшей доли
    int crash_next;         // сбивка кончилась: крэш на следующем шаге 0
    uint8_t perturb[3];
    uint32_t rnd;           // ГПСЧ (LCG), сид в init - детерминизм тестов
} acid_drummer_t;
void acid_drummer_init(acid_drummer_t *d); // умолчания из таблицы спеки
// Ручка enc 1..12; вне списка - игнор (возвращает 0), принято - 1.
int acid_drummer_set(acid_drummer_t *d, int enc, int v);
void acid_drummer_reset(acid_drummer_t *d); // Start/Stop: сбивки и крэш сброшены
// Шаг step 0..15 такта bar: ноты в out, вернуть их число (0, если выключен).
int acid_drummer_step(acid_drummer_t *d, int step, uint32_t bar, acid_hit_t out[ACID_DRUMMER_MAX]);
```
Умолчания: вкл 0, X 64, Y 64, плотности 80/70/90, хаос 0, хэт 0, время 0, каждые 2 (8 тактов), длина 0 (1 доля), сейчас 0. Шаг Grids = `(bar & 1) * 16 + step`; возмущение `rnd % 256 * (хаос_u8 >> 2) >> 8` на дорожку в начале каждого 32-шагового паттерна.

Правила (из спеки), которые тесты прибивают:
- Бочка 36 по дорожке 0. Малый 38 по дорожке 1.
- Время по дорожке 2: хэт - закр. 42 / откр. 46 / вперемешку (акцент 46, иначе 42); райд - 51, акцент 53. В режиме райд педаль 44 на шагах 4 и 12, vel 50.
- Сбивка: шаги `[fill_from, fill_to)`. Время молчит. Руки: малый по дорожке 1 и том по дорожке 2, плотности этих дорожек +128 с насыщением. Том по позиции в сбивке сверху вниз: ноты `50, 48, 47, 45, 43, 41`, индекс `6 * (step - fill_from) / (fill_to - fill_from)`. Бочка как обычно.
- Авто: N = `{0, 4, 8, 16}[каждые]`, L = `{1, 2, 4}[длина] * 4` шагов; в такте с `(bar + 1) % N == 0` сбивка `[16 - L, 16)`.
- «Сейчас»: переход 0 -> 1 ставит `fill_pending`; на ближайшем шаге с `step % 4 == 0` сбивка `[step, 16)`. Если сбивка уже идёт - игнор.
- После последнего шага сбивки: шаг 0 следующего такта - 49 и 36 (бочка даже без удара Grids), время молчит.
- Velocity: акцент 120, иначе 90, плюс `rnd` в -8..+8; педаль и крэш без разброса не обязаны.

- [ ] **Step 1: тесты** `host/test_drummer.c` (точки карты и плотности подобрать, чтобы дорожки били; ассерты по правилам):
  - `test_off_silent`: вкл 0 - 0 нот на 64 шагах.
  - `test_limbs`: на 2 тактах по 50 случайным X/Y/плотностям: не больше 4 нот на шаг, не больше 2 ручных (всё кроме 36 и 44), нет 42/46 вместе с 51/53.
  - `test_hat_modes`: закр. - только 42; откр. - только 46; вперемешку - 46 ровно на акцентах дорожки 2.
  - `test_ride`: время райд - хэт-нот 42/46 нет, есть 51, на акцентах 53, на шагах 4 и 12 - 44.
  - `test_auto_fill`: каждые 4, длина 1: в такте 3 шаги 12-15 без времени, с томами сверху вниз; в такте 4 шаг 0 - 49 и 36.
  - `test_fill_now`: «сейчас» на шаге 5 - сбивка 8..15; повторное «сейчас» на шаге 9 - ничего не меняет; на шаге 13 (последняя доля) - сбивка начинается в следующем такте с шага 0 до 16 (Review Focus 3).
  - `test_fill_now_needs_release`: 1 -> 1 вторая сбивка не запускается; 1 -> 0 -> 1 запускается.
  - `test_list_ignore`: хэт 3, время 2, каждые 4, длина 3 - `acid_drummer_set` = 0, ручка прежняя.
  - `test_deterministic`: два init подряд с хаосом 127 дают одинаковые ноты.
- [ ] **Step 2:** FAIL. **Step 3:** реализовать. **Step 4:** все хост-тесты PASS.
- [ ] **Step 5:** Коммит `feat: drummer turns grids hits into four-limb notes and fills`.

### Task 4: Синт: страница Grids и игра от клока

**Files:**
- Modify: `components/acid/include/acid_synth.h` (`ACID_PAGES 13`, `#define ACID_PAGE_GRIDS 12`, поля `acid_beat_t beat; acid_drummer_t drummer; int grids_step;` и счётчик `uint32_t grids_hits` для смоука)
- Modify: `components/acid/acid_synth.c`: `acid_synth_init`, `acid_page_to_bsp`, `apply_page`, `acid_synth_knob`, `acid_synth_midi` (CLOCK, START, CONTINUE, STOP), `RAW_DEFAULT`
- Test: `host/test_grids_synth.c`

**Interfaces:**
- Consumes: Task 2, Task 3.
- Produces: `ACID_PAGE_GRIDS`; `s->grids_step` - шаг паттерна Grids 0..31 последнего сыгранного шага, -1 - выключен или стоп (для панели); `s->grids_hits` - нот от Grids с init.

- [ ] **Step 1: тесты** `host/test_grids_synth.c`:
  - `test_pages`: `acid_page_to_bsp(ACID_PAGE_GRIDS) == -1`; `acid_page_to_bsp(10) == 16`, `(11) == 17`; `acid_page_from_bsp(v) != ACID_PAGE_GRIDS` для v 0..127 (Review Focus 5).
  - `test_knob_mirror`: `acid_synth_set_knob(s, ACID_PAGE_GRIDS, DRM_X, 30)` -> `acid_synth_knob(...) == 30`; список вне диапазона не меняет; умолчания по таблице спеки.
  - `test_plays_on_clock`: вкл 1, Start, 96 тиков - `grids_hits > 0`, бочка звучит (`s->kick_vel` обновился или счётчик по ноте 36 через `grids_hits`); вкл 0 - `grids_hits` не растёт.
  - `test_enable_mid_bar`: 40 тиков с вкл 0, затем вкл 1 - на следующих 6 тиках не больше одного шага нот (Review Focus 2).
  - `test_stop_silent`: Stop - тики не дают нот, `grids_step == -1`.
  - `test_bsp_drums_pass`: при вкл Grids нота 38 канала 2 от BSP триггерит малый, как раньше.
- [ ] **Step 2:** FAIL. **Step 3:** реализовать:
  - `acid_page_to_bsp`: слои только `[ACID_PAGE_LAYER1, ACID_PAGE_LAYER1 + ACID_SLOTS)`; Grids - -1.
  - `apply_page`: страница Grids - `acid_drummer_set`, при успехе `s->raw[ACID_PAGE_GRIDS][enc] = v`.
  - Тик клока: `if (acid_beat_tick(&s->beat))` -> `acid_drummer_step` -> каждая нота в `drum_on()`, `grids_hits += n`, `grids_step` обновить. Start/Continue/Stop - `acid_beat_*` и `acid_drummer_reset`.
- [ ] **Step 4:** все хост-тесты PASS, включая старые (`test_knob_mirror`, `test_link`, `test_live`). Демо акида и метала бит в бит как до задачи (процедура из `CLAUDE.md`, сравнить с HEAD до задачи; Grids по умолчанию выключен).
- [ ] **Step 5:** Коммит `feat: grids page plays drummer notes on MIDI clock`.

### Task 5: Сцены и свои страницы

**Files:**
- Modify: `components/acid/acid_scene.c` (`MAGIC` -> `{'S','C','N',3}`, перенос v1 и v2 одной функцией по числу страниц; store пишет `UNSET` в Grids `DRM_ON` и `DRM_FILL_NOW`)
- Test: `host/test_scene.c` (дописать)

**Interfaces:**
- Consumes: `ACID_PAGE_GRIDS`, `DRM_ON`, `DRM_FILL_NOW`.

- [ ] **Step 1: тесты** в `host/test_scene.c`:
  - `test_scene_skips_grids_actions`: Grids вкл, X 30; store; вкл 0, X 90; recall без клока - X 30, вкл остался 0.
  - `test_scene_v2_import`: собрать blob v2 (магия `SCN2`, 12 страниц, байты как в текущем `acid_scene_export`), import -> 1, страницы 0-11 совпадают, страница Grids вся `UNSET`, вызов не трогает Grids (Review Focus 4).
  - `test_scene_v1_import` (уже есть) остаётся зелёным.
  - `test_custom_targets_grids`: `acid_custom_edit(c, 0, 1, 0, ACID_PAGE_GRIDS, DRM_FILL_NOW, 0, 127)` -> 1.
- [ ] **Step 2:** FAIL. **Step 3:** реализовать. **Step 4:** PASS.
- [ ] **Step 5:** Коммит `feat: scenes v3 keep grids sound, skip grids actions`.

### Task 6: Протокол, связка, подписи

**Files:**
- Modify: `components/panel_link/include/pl_proto.h` (`PL_PAGES 13`, `PL_PAGE_LAYER1 10`, `PL_PAGE_GRIDS 12`, `PL_USER_FIRST 13`, `PL_GRIDS_STEP = 12`), `components/panel_link/pl_proto.c` (`pl_page_bsp`: слои через `PL_PAGE_LAYER1`, Grids -1)
- Modify: `components/panel_link/pl_labels.c` (страница 12: имя `GRIDS`, подписи `ON, X, Y, KICK, SNARE, HAT, CHAOS, HAT MODE, TIME, EVERY, FILL LEN, FILL`, слова `OFF/ON`, `CLOSED/OPEN/MIX`, `HAT/RIDE`, `OFF/4/8/16`, `1/2/4`, ходы списков)
- Modify: `components/panel_link/pl_model.c` (кадр `PL_GRIDS_STEP` -> `m->grids_step`), `pl_model.h`
- Modify: `main/panel_uart.c` (слать `PL_GRIDS_STEP`, 1 байт, при смене `s->grids_step`)
- Modify: `components/acid/acid_synth.c` или общий заголовок: `_Static_assert(ACID_PAGES == PL_PAGES && ACID_PAGE_GRIDS == PL_PAGE_GRIDS)`
- Test: `host/test_proto.c`, `host/test_labels.c`, `host/test_model.c` (дописать)

**Interfaces:**
- Produces: кадр `PL_GRIDS_STEP` (синт -> панель, нагрузка 1 байт: шаг 0..31 или 0xFF), `pl_model_t.grids_step` (-1 нет данных или стоп).

- [ ] **Step 1: тесты:**
  - `test_snapshot_fits`: `sizeof(pl_snapshot_t) <= PL_PAYLOAD_MAX` (253).
  - `test_page_bsp`: `pl_page_bsp(10) == 16`, `(11) == 17`, `(PL_PAGE_GRIDS) == -1`, `(PL_USER_FIRST) == 10`.
  - `test_grids_step_frame`: кодирование/разбор, чужая длина отброшена; модель берёт шаг.
  - `test_grids_labels`: подписи и слова страницы 12 по списку выше, `pl_knob_range(12, DRM_HAT_MODE) == 0..2`, X - 0..127.
- [ ] **Step 2:** FAIL. **Step 3:** реализовать. **Step 4:** все хост-тесты PASS (старые тесты своих страниц - с новыми номерами через `PL_USER_FIRST`, без хардкода 12).
- [ ] **Step 5:** `idf.py build` синта и `idf.py -C panel build` - без ошибок.
- [ ] **Step 6:** Коммит `feat: grids page in panel protocol, labels and step frame`.

### Task 7: Экран Grids на панели

**Files:**
- Create: `panel/main/ui_grids.c` (объявления в `ui_priv.h`, по образцу `uc_*` в `ui_custom.c`)
- Modify: `panel/main/ui.c` (меню 1 без Grids: `MENU_SLOT` на 13 страниц, Grids пропускается; листание страниц пропускает Grids; плитка `GRIDS` во втором меню после `SCENES`, `MENU2_TILES` +1; экран `GRIDS` в переключении видов), `panel/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `pl_grids_preview`, `pl_xy_*` (`hold = 1`), `pl_model_t.raw[PL_PAGE_GRIDS]`, `grids_step`, отправка `PL_SET` как у карточек ручек; при открытии экрана - `PL_PAGE 12` (энкодеры BSP 2-13 крутят Grids, пока не тронут энкодер 1).

Раскладка (рабочая область 480 x 440, под шапкой):
```
[ ON ]  [ FILL (держать) ]  HAT: CLOSED|OPEN|MIX   TIME: HAT|RIDE
+----------XY 200x200----------+   KICK  SNARE  HAT  CHAOS
|            точка             |   ||    ||     ||   ||     вертикальные ползунки
+------------------------------+
EVERY: OFF|4|8|16    LEN: 1|2|4
K ▪▪·▪··▪▪·····▪···▪▪·▪··▪▪·····▪··   3 x 32 клетки, акцент ярче,
S ····▪·······▪·······▪·······▪···   бегущий шаг - рамка
H ▪·▪·▪·▪·▪·▪·▪·▪·▪·▪·▪·▪·▪·▪·▪·▪·
```
Точная вёрстка - на усмотрение, в стиле существующих экранов (`C_*`, шрифты Montserrat).

- [ ] **Step 1:** Логику, которую можно проверить на ПК (сегменты списков: точка -> значение, ползунок: y -> 0..127), вынести в `components/panel_link` и покрыть хост-тестом до кода, если она не сводится к существующим `pl_xy`/`pl_gesture`.
- [ ] **Step 2:** Сборка `idf.py -C panel build`.
- [ ] **Step 3:** Прошить панель (`idf.py -C panel -p /dev/ttyUSB0 -b 460800 flash`, не прерывать) и синт, проверить: вход с плитки, вкл, XY, ползунки, сегменты, FILL, сетка и бегущий шаг при секвенции BSP; `lv_mem_monitor` после построения экрана в логе панели - свободно не меньше 10 КБ.
- [ ] **Step 4:** Коммит `feat: panel grids screen`.

### Task 8: Демо и смоук

**Files:**
- Modify: `components/acid/include/acid_midi.h` (`ACID_MIDI_KNOB`: ch - страница, d1 - enc, d2 - значение; только внутренний, из USB не приходит)
- Modify: `components/acid/acid_demo.c` (ручка демо со страницей без номера BSP шлёт `ACID_MIDI_KNOB` вместо выбора страницы и CC), `components/acid/acid_synth.c` (`ACID_MIDI_KNOB` -> `acid_synth_set_knob`), `components/acid/acid_link.c` (`ACID_MIDI_KNOB` от демо подчиняется `demo_muted`, как CC)
- Modify: `components/acid/acid_metal_song.c`, `components/acid/acid_demo_song.c` (в каждой песне часть с Grids: вкл, ударка части `NULL`, сбивка каждые 4, в одной из частей - райд или вперемешку; на выходе из части вкл 0)
- Test: `host/test_demo.c`, `host/test_demo_smoke.c`

- [ ] **Step 1: тесты:** `test_demo` - ручка демо на `ACID_PAGE_GRIDS` даёт событие `ACID_MIDI_KNOB`; `test_demo_smoke` - в каждой песне `grids_hits > 0`, был крэш после сбивки от Grids (нота 49 при `drum_on` из барабанщика: счётчик в `seen_t` через `s.grids_hits` и состояние `s.drummer.crash_next`, на усмотрение), райд от Grids хотя бы в одной песне.
- [ ] **Step 2:** FAIL. **Step 3:** реализовать. **Step 4:** все хост-тесты PASS; `acid_render` обеих песен без NaN и тишины (смоук это проверяет).
- [ ] **Step 5:** Коммит `feat: demo songs play a grids part`.

### Task 9: Документы и проверка на плате

**Files:**
- Modify: `README.md` (возможности, таблица ручек страницы Grids, лицензия GPLv3, атрибуция Grids), `docs/controls.html` (`PAGES` с Grids, карточка Grids), `docs/roadmap.md` (этап 15 сделан; проект «аранжировка» следующим, после SD панели), `CLAUDE.md` (если появились грабли)

- [ ] **Step 1:** Документы по списку выше. Коммит `docs: grids auto-drummer`.
- [ ] **Step 2:** Плата: `idf.py -p /dev/ttyACM0 flash` синт и прошивка панели; секвенция на BSP, Grids вкл с панели - пройти карту, плотности, режимы хэта, райд, «сбивку сейчас», авто-сбивки, подыграть пэдами BSP. В логе раз в 10 с: защита не включается, ядро 1 в обычной игре как до этапа (± шум), ядро 0 не выше ~600 из 666 на кусок.
- [ ] **Step 3:** Демо метал и акид на плате: защита как раньше (метал не включает, акид на пике - ступень 1). Цифры записать в `CLAUDE.md`/`roadmap.md`, если изменились. Коммит.
- [ ] **Step 4:** Отдать пользователю на прослушку: стартовые плотности, velocity, педаль в райде - на слух.
