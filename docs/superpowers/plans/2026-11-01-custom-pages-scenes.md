# Свои страницы, макро, сцены: план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 6 своих страниц (энкодер 1 BSP = 10..15) из любых ручек, с псевдонимами и макро до 4 целей, и 8 сцен
с вызовом сразу или переходом по тактам; редактор и сцены на панели.

**Architecture:** На синте два чистых модуля: `acid_custom` (определения и маппинг) и `acid_scene` (снимок
и переход). Оба живут в `main.c` рядом с синтом и переживают смену демо/live; `acid_link` получает
`acid_custom_t *` и маршрутизирует своё. Панель получает новые кадры, логика редактора - чистый `pl_edit`
в `panel_link`, LVGL только рисует. Рендер не трогается.

**Tech Stack:** C11, ESP-IDF 6.1, LVGL на панели, хост-тесты `host/test.h`.

**Spec:** `docs/superpowers/specs/2026-11-01-custom-pages-scenes-design.md`

## Global Constraints

- TDD: хост-тест до кода, увидеть красный, потом зелёный. Тесты `host/test_*.c` подхватываются glob-ом сами.
- Индексы энкодеров в коде с 0, как в `acid_link` и `acid_synth_knob`: 0 - энкодер 1 (страница), слоты 1..15.
- Комментарии по-русски, код и git по-английски, без длинных тире. Коммит каждой задачи, `antifreezzz <me@kvatech.ru>`.
- В рендере (`acid_synth_render*`) ничего не менять. Демо бит в бит с `main`.
- `BLOB_MAGIC` в `acid_synth.c` не трогать: свои страницы и сцены - свои ключи NVS и своя магия.
- Не делить float на каждом сэмпле и не звать libm в горячем цикле (здесь горячего цикла нет, но обработка
  событий идёт в куске 666 мкс ядра 1: на кусок не больше `ACID_SCENE_PER_CHUNK` применённых ручек).

## Review Focus

- Энкодер 1 BSP = 10..15, а страница пустая или `custom == NULL`: ничего не звучит, падения нет, значение
  слота запоминается (тест в задаче 2).
- Энкодер 1 BSP с 12 обратно на 6: энкодеры 2..16 снова крутят синт 1, своя страница больше не активна (задача 2).
- Макро с lo > hi и крайние значения: v = 0 даёт ровно lo, v = 127 ровно hi (задача 1).
- Вызов сцены во время идущего перехода: новый переход стартует от текущих значений, без прыжка назад (задача 3).
- Битый или старый blob из NVS (`custom`, `scenes`): отвергается, всё пусто, не применяется наполовину (задачи 1, 3).

---

### Task 1: модуль `acid_custom`

**Files:**
- Create: `components/acid/include/acid_custom.h`, `components/acid/acid_custom.c`
- Modify: `components/acid/CMakeLists.txt` (добавить `acid_custom.c` в SRCS)
- Test: `host/test_custom.c`

**Interfaces:**
- Produces:
```c
#define ACID_USER_PAGES 6
#define ACID_USER_FIRST 10   // энкодер 1 BSP: 10..15
#define ACID_USER_TARGETS 4
#define ACID_CUSTOM_NONE 0xFF
typedef struct { uint8_t page, enc, lo, hi; } acid_target_t; // page == ACID_CUSTOM_NONE - пусто
typedef struct {
    acid_target_t t[ACID_USER_PAGES][ACID_KNOBS][ACID_USER_TARGETS]; // [.][0][.] не используется
    uint8_t val[ACID_USER_PAGES][ACID_KNOBS];  // своё значение слота, умолчание 0
    int save;                                   // правка: пора записать во flash
} acid_custom_t;
#define ACID_CUSTOM_BLOB (4 + sizeof(((acid_custom_t *)0)->t) + sizeof(((acid_custom_t *)0)->val))
void acid_custom_init(acid_custom_t *c);
// Поставить цель idx слота (up 0..5, enc 1..15). page 0..9, enc 1..15; enc 1 на страницах 6-7 (тип) нельзя.
// page == ACID_CUSTOM_NONE - убрать цель (остальные сдвигаются вверх). 1 - принято, 0 - отвергнуто.
int acid_custom_edit(acid_custom_t *c, int up, int enc, int idx, int page, int tenc, int lo, int hi);
int acid_custom_count(const acid_custom_t *c, int up, int enc);     // целей у слота
int acid_custom_is_alias(const acid_custom_t *c, int up, int enc);  // одна цель, lo 0, hi 127
int acid_custom_map(const acid_target_t *t, int v);                 // lo + (hi - lo) * v / 127, к ближайшему
// Значение слота для экрана: у псевдонима - живое значение цели, иначе val.
int acid_custom_value(const acid_custom_t *c, const acid_synth_t *s, int up, int enc);
size_t acid_custom_export(const acid_custom_t *c, void *buf, size_t cap);
int acid_custom_import(acid_custom_t *c, const void *buf, size_t len); // 1 - применено, 0 - чужой
```
Магия blob: `{'C', 'U', 'S', 1}`.

- [ ] **Step 1: Write the failing tests** в `host/test_custom.c`:
  - `test_map_ends_and_inverse`: цель {0,1,10,90}: map(0) == 10, map(127) == 90, map(64) == 50;
    цель {0,1,100,20}: map(0) == 100, map(127) == 20.
  - `test_edit_rejects_bad_targets`: edit с page 10, page 6 enc 1, page 7 enc 1, tenc 0, tenc 16, up 6, enc 0,
    idx 4 возвращают 0; page 6 enc 2 возвращает 1 и count == 1.
  - `test_remove_shifts`: три цели, убрать idx 0: count == 2, бывшая цель 1 теперь в t[..][0].
  - `test_alias`: одна цель {6,2,0,127} - alias; {6,2,0,100} - нет; две цели по 0..127 - нет.
  - `test_value_alias_follows_target`: synth init, `acid_synth_set_knob(&s, 6, 2, 77)`, слот-псевдоним на (6,2):
    `acid_custom_value` == 77; у макро - `val`.
  - `test_blob_roundtrip_and_reject`: export/import восстанавливает t и val; испорченная магия или длина - 0
    и c не меняется.
  - `test_edit_sets_save`: успешный edit ставит save = 1, отвергнутый - нет.
- [ ] **Step 2: Run** `cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build -R test_custom`
  Expected: сборка падает (нет `acid_custom.h`).
- [ ] **Step 3: Implement** `acid_custom.c` по интерфейсу. Округление `map`: `lo + ((hi - lo) * v + (hi >= lo ? 63 : -63)) / 127`.
- [ ] **Step 4: Run** те же команды, Expected: `test_custom` PASS, остальные тесты PASS.
- [ ] **Step 5: Commit** `git commit -m "acid: custom pages model with aliases and macros"`

### Task 2: свои страницы в `acid_link`

**Files:**
- Modify: `components/acid/include/acid_link.h`, `components/acid/acid_link.c`
- Test: `host/test_link.c` (новые тесты в конец, старые не трогать)

**Interfaces:**
- Consumes: Task 1 (`acid_custom_t`, `acid_custom_map`, `acid_custom_is_alias`, `acid_custom_value`).
- Produces:
```c
// в acid_link_t:
uint8_t held[ACID_PAGES + ACID_USER_PAGES][ACID_KNOBS]; // было [ACID_PAGES]
acid_custom_t *custom; // NULL - своих страниц нет; init не трогает (ставит acid_link_use_custom)
int user;              // активная своя страница 0..5, -1 - штатная s->page
acid_link_ev_t pend[8]; int npend; // события для панели сверх первого
uint8_t custom_due;    // бит up: панели нужен кадр PL_CUSTOM этой страницы
void acid_link_use_custom(acid_link_t *l, acid_custom_t *c);
int acid_link_take(acid_link_t *l, acid_link_ev_t *out); // 1 - ещё одно событие для панели
int acid_link_page(const acid_link_t *l, const acid_synth_t *s); // страница BSP: 10 + user или s->page
int acid_link_custom_edit(acid_link_t *l, int up, int enc, int idx, int page, int tenc, int lo, int hi);
```
`acid_link_init` обнуляет `user = -1`, `npend`, `custom_due`, `held`, но не `custom`.

Поведение:
- CC энкодера 1 со значением 10..15: `user = v - 10` (если `custom != NULL`, иначе игнор как раньше),
  событие `{page = v, enc = 0, value = v}`. Значение 0..9: `user = -1`, дальше как сейчас.
- CC энкодеров 2..16 при `user >= 0`: подхват как сейчас, но по `held[10 + user][enc]` и значению
  `acid_custom_value`; потом слот: псевдоним - `acid_synth_set_knob(цель, v)`, макро - `val = v` и каждая
  цель получает `acid_custom_map(t, v)`; `custom->save = 1`. Первое событие - слот (`page = 10 + user`),
  в `pend` - по событию на каждую цель (её страница, value = `acid_synth_knob`).
- `acid_link_panel_set` с page 10..15: то же применение, `held[page][enc] = 1`, `demo_muted = 1`.
- `acid_link_panel_page` с page 10..15: `user = page - 10`; 0..9: `user = -1` и как сейчас.
- `acid_link_custom_edit`: `acid_custom_edit`, при успехе `custom_due |= 1 << up`.

- [ ] **Step 1: Write the failing tests** (хелпер `reset()` зовёт `acid_link_use_custom(&l, &c)` после `acid_custom_init`):
  - `test_user_page_macro_from_bsp`: макро слот (0, 2) с целями (6,2,0,127) и (0,1,127,0); bsp(1,10); bsp(3,127):
    knob(6,3) == 127, knob(0,2) == 0, ev.page == 10, `acid_link_take` выдаёт два события целей, третий вызов 0.
  - `test_user_page_alias_from_bsp`: псевдоним на (6,2); bsp(1,10); bsp(3,40): knob(6,3) == 40, ev.value == 40.
  - `test_user_page_empty_slot`: bsp(1,15); bsp(5,99): ни одна штатная ручка не изменилась, `c.val[5][4] == 99`.
  - `test_user_page_no_custom`: без `use_custom` bsp(1,12), bsp(3,50): ведёт себя как сейчас (страница 6 не сменилась,
    knob(6,3) == 50 - BSP всё ещё на синте 1).
  - `test_back_to_standard_page`: bsp(1,10); bsp(1,6); bsp(3,33): knob(6,3) == 33, `l.user == -1`.
  - `test_user_page_pickup`: panel(10, 3, 30) при bsp_pos 90: bsp(1,10); bsp(3,80) не меняет цель; bsp(3,29) меняет.
  - `test_link_init_keeps_custom`: после `acid_link_init` `l.custom` прежний, `l.user == -1`.
  - `test_page_reports_user`: после bsp(1,11) `acid_link_page` == 11.
- [ ] **Step 2: Run** `cmake --build host/build && ctest --test-dir host/build -R test_link`, Expected: FAIL (сборка).
- [ ] **Step 3: Implement** в `acid_link.c`.
- [ ] **Step 4: Run** весь `ctest --test-dir host/build`, Expected: всё PASS.
- [ ] **Step 5: Commit** `git commit -m "acid: link routes BSP and panel through custom pages"`

### Task 3: модуль `acid_scene`

**Files:**
- Create: `components/acid/include/acid_scene.h`, `components/acid/acid_scene.c`
- Modify: `components/acid/CMakeLists.txt` (SRCS `acid_scene.c`, `REQUIRES plaits panel_link` - нужен `pl_knob_range`)
- Test: `host/test_scene.c`

**Interfaces:**
- Consumes: Task 1, Task 2 (`acid_link_panel_set` для каждой ручки: помечает held, глушит демо),
  `pl_knob_range` из `pl_labels.h` (ручка-список: ход не 0..127), `s->tonal.phase` (тик в такте, 96 на такт).
- Produces:
```c
#define ACID_SCENES 8
#define ACID_SCENE_PER_CHUNK 8   // ручек на кусок
#define ACID_SCENE_NOCLOCK 750   // кусков без тика (0.5 с): клока нет, вызов сразу
#define ACID_SCENE_SNAP_EVERY 150 // кусков между снимками панели во время перехода (100 мс)
typedef struct {
    uint8_t knob[ACID_SCENES][ACID_PAGES][ACID_KNOBS];      // энкодеры 1..15
    uint8_t user[ACID_SCENES][ACID_USER_PAGES][ACID_KNOBS];
    uint8_t used;      // бит слота
    int save;
    // переход
    int cur;           // вызванная сцена, -1 нет
    int state;         // 0 нет, 1 ждём границу такта, 2 идёт
    int bars, ticks, last_phase, idle_chunks, cursor, since_snap;
    uint8_t from[ACID_PAGES][ACID_KNOBS];
} acid_scene_t;
#define ACID_SCENE_BLOB (4 + 1 + sizeof(((acid_scene_t *)0)->knob) + sizeof(((acid_scene_t *)0)->user))
void acid_scene_init(acid_scene_t *sc);
void acid_scene_store(acid_scene_t *sc, int n, const acid_synth_t *s, const acid_custom_t *c); // save = 1
int acid_scene_recall(acid_scene_t *sc, int n, int bars, const acid_synth_t *s); // 0 - пустой слот или вне 0..7
// Каждый кусок перед рендером. 1 - пора слать панели снимок (не чаще ACID_SCENE_SNAP_EVERY и в конце).
int acid_scene_step(acid_scene_t *sc, acid_link_t *l, acid_synth_t *s);
size_t acid_scene_export(const acid_scene_t *sc, void *buf, size_t cap);
int acid_scene_import(acid_scene_t *sc, const void *buf, size_t len); // магия {'S','C','N',1}
```
Шаг (`acid_scene_step`): `idle_chunks` растёт без смены `tonal.phase`; state 1 -> 2 на обороте фазы
(или сразу, если `idle_chunks >= ACID_SCENE_NOCLOCK` или bars == 0 и клока нет). В начале state 2: сперва типы
(страницы 6-7 энкодер 1), потом списки и `bars == 0` - всё значением сцены, `custom->val` из `user[n]`.
Дальше непрерывные ручки `from + (to - from) * min(1, ticks / (bars * 96))`, применяются только изменившиеся,
не больше `ACID_SCENE_PER_CHUNK` за кусок всех видов вместе, курсор по кругу. Конец - все ручки на значениях
сцены: state 0. Новый recall при state != 0: `from` берётся из текущих `acid_synth_knob`.

- [ ] **Step 1: Write the failing tests** в `host/test_scene.c` (клок: `acid_synth_midi` с `ACID_MIDI_START`, затем
  `ACID_MIDI_CLOCK`, хелпер `tick(n)` даёт n тиков и по шагу на тик):
  - `test_store_recall_instant_no_clock`: store 0 при knob(6,3) == 20, поставить 90, recall(0, 4) без клока,
    шагать до state 0: knob(6,3) == 20; шагов не меньше ceil(изменённых / 8).
  - `test_per_chunk_limit`: сцена отличается во всех ручках страниц 0-2: после одного шага изменено <= 8 ручек.
  - `test_waits_for_bar`: Start, tick(10), recall(0, 0): до тика 96 ручка прежняя, после - значение сцены.
  - `test_morph_linear`: from 0 к 96 по непрерывной (0,1) мастер, bars 1: после 48 тиков значение 48 +-2.
  - `test_list_jumps_at_start`: дилэй время (8,6) 0 -> 4 при bars 4: после первого тика перехода уже 4.
  - `test_type_first`: сцена с типом синта 2 = 11 (FM) и ручкой (7,3) = 99: после перехода sel_type 11 и knob(7,3) == 99.
  - `test_recall_during_morph_starts_from_current`: в середине перехода recall другой сцены: значение не прыгает
    назад к началу первой (разница со значением до вызова <= 2).
  - `test_empty_slot_ignored`: recall(3) без store - 0, state 0.
  - `test_snap_rate`: за переход на 2 такта `acid_scene_step` вернул 1 не чаще раза в 150 шагов и 1 на последнем.
  - `test_blob_roundtrip_and_reject`: как в задаче 1.
- [ ] **Step 2: Run** `cmake --build host/build && ctest --test-dir host/build -R test_scene`, Expected: FAIL (сборка).
- [ ] **Step 3: Implement** `acid_scene.c`.
- [ ] **Step 4: Run** весь `ctest`, Expected: всё PASS.
- [ ] **Step 5: Commit** `git commit -m "acid: scenes with bar-synced morph and per-chunk knob limit"`

### Task 4: протокол и модель панели

**Files:**
- Modify: `components/panel_link/include/pl_proto.h`, `pl_model.h`, `pl_model.c`, `pl_labels.h`, `pl_labels.c`
- Test: `host/test_proto.c`, `host/test_model.c`, `host/test_labels.c`

**Interfaces:**
- Produces:
```c
#define PL_USER_PAGES 6
#define PL_USER_FIRST 10
#define PL_TARGETS 4
#define PL_NONE 0xFF
enum { PL_CUSTOM = 6, PL_CUSTOM_EDIT = 7, PL_SCENE = 8, PL_SCENE_STATE = 9 };
typedef struct { uint8_t pe, lo, hi; } pl_target_t;  // pe = page << 4 | enc, PL_NONE - пусто
typedef struct {
    uint8_t up;                                   // 0..5
    pl_target_t t[PL_KNOBS - 1][PL_TARGETS];       // слоты энкодеров 1..15
    uint8_t val[PL_KNOBS - 1];                     // значение слота (у псевдонима - цели)
    uint8_t held[2];                               // бит enc - 1
} pl_custom_t;                                     // 198 байт
typedef struct { uint8_t up, enc, idx, pe, lo, hi; } pl_custom_edit_t; // pe PL_NONE - убрать
typedef struct { uint8_t op, n, bars; } pl_scene_t;  // op 0 вызвать, 1 записать
typedef struct { uint8_t used, cur, state; } pl_scene_state_t; // cur PL_NONE - нет
// в pl_model_t:
pl_target_t user_t[PL_USER_PAGES][PL_KNOBS][PL_TARGETS];
uint8_t user_val[PL_USER_PAGES][PL_KNOBS], user_held[PL_USER_PAGES][PL_KNOBS];
int user_have;                 // бит up: кадр страницы пришёл
pl_scene_state_t scene;
// подписи
const char *pl_user_name(int up);  // "USER 1".."USER 6", NULL вне
// Подпись слота: подпись первой цели (pl_knob_label по raw её страницы) и " +N" у макро с N > 1; NULL - пусто.
const char *pl_slot_label(const pl_target_t t[PL_TARGETS], const uint8_t raw[PL_PAGES][PL_KNOBS], char buf[20]);
```
- `pl_model_apply`: `PL_CUSTOM` пишет `user_*`, `user_have |= 1 << up`; `PL_KNOB` с page 10..15 пишет `user_val`
  и `user_held`, тост и `goto_page` как у штатных (enc 0 со значением 10..15 - смена страницы BSP);
  `PL_SCENE_STATE` пишет `scene`.
- `pl_model_local_set` принимает page 10..15 (нужен `user_have` этой страницы).

- [ ] **Step 1: Write the failing tests:**
  - `test_proto.c` `test_new_frame_sizes`: `sizeof(pl_custom_t) == 198`, `sizeof(pl_custom_edit_t) == 6`,
    `sizeof(pl_scene_t) == 3`, `sizeof(pl_scene_state_t) == 3`; кадр `PL_CUSTOM` проходит encode -> rx.
  - `test_model.c` `test_custom_frame_applied`, `test_user_knob_and_page_change`, `test_local_set_user_needs_frame`,
    `test_scene_state_applied`, `test_custom_wrong_len_skipped`.
  - `test_labels.c` `test_slot_label`: одна цель (6,2) при 303 - "CUTOFF"; три цели - "CUTOFF +2"; пусто - NULL;
    `pl_user_name(0)` == "USER 1", `pl_user_name(6)` == NULL.
- [ ] **Step 2: Run** `ctest --test-dir host/build -R "test_proto|test_model|test_labels"`, Expected: FAIL.
- [ ] **Step 3: Implement.** Длины кадров уже различаются по типу (правило «своя длина у типа»): проверить в `pl_proto.c`,
  если там таблица длин - дописать новые типы.
- [ ] **Step 4: Run** весь `ctest`, Expected: PASS.
- [ ] **Step 5: Commit** `git commit -m "panel_link: custom page and scene frames, model and labels"`

### Task 5: логика редактора `pl_edit`

**Files:**
- Create: `components/panel_link/include/pl_edit.h`, `components/panel_link/pl_edit.c`
- Modify: `components/panel_link/CMakeLists.txt` (SRCS)
- Test: `host/test_edit.c`

**Interfaces:**
- Consumes: Task 4 (`pl_model_t`, `pl_target_t`, `pl_custom_edit_t`, `pl_knob_range`).
- Produces:
```c
enum { PL_EDIT_OFF, PL_EDIT_SLOTS, PL_EDIT_TARGETS, PL_EDIT_PICK_PAGE, PL_EDIT_PICK_KNOB, PL_EDIT_RANGE };
typedef struct { int state, up, enc, idx, page, which; } pl_edit_t; // which: 0 lo, 1 hi
void pl_edit_start(pl_edit_t *e, int up);        // EDIT в шапке своей страницы -> SLOTS
void pl_edit_slot(pl_edit_t *e, int enc);        // тап по карточке -> TARGETS
void pl_edit_add(pl_edit_t *e, const pl_model_t *m); // «+» -> PICK_PAGE, если целей < 4
void pl_edit_page(pl_edit_t *e, int page);       // 0..9 -> PICK_KNOB
// Выбор ручки -> RANGE; out - правка с lo/hi по умолчанию (ход ручки, pl_knob_range). 1 - слать синту.
int pl_edit_knob(pl_edit_t *e, const pl_model_t *m, int enc, pl_custom_edit_t *out);
int pl_edit_range(pl_edit_t *e, const pl_model_t *m, int v, pl_custom_edit_t *out); // крутилка lo или hi
int pl_edit_remove(pl_edit_t *e, int idx, pl_custom_edit_t *out);  // pe = PL_NONE
void pl_edit_back(pl_edit_t *e);                 // шаг назад, из SLOTS -> OFF
```
- [ ] **Step 1: Write the failing tests** `host/test_edit.c`: путь SLOTS -> TARGETS -> PICK_PAGE -> PICK_KNOB -> RANGE
  с правкой `{up, enc, idx = число целей, pe = 6 << 4 | 2, lo 0, hi 127}`; список (8,6) даёт lo 0 hi 4;
  тип (6,1) не выбирается (`pl_edit_knob` 0); при 4 целях `pl_edit_add` не уходит из TARGETS; `pl_edit_back` по шагам до OFF;
  `pl_edit_range` меняет только `which`.
- [ ] **Step 2: Run** `ctest --test-dir host/build -R test_edit`, Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** весь `ctest`, Expected: PASS.
- [ ] **Step 5: Commit** `git commit -m "panel_link: custom page editor state machine"`

### Task 6: прошивка синта: хранение, кадры, шаг сцены

**Files:**
- Modify: `main/store.c`, `main/store.h`, `main/main.c`, `main/panel_uart.c`, `main/panel_uart.h`

**Interfaces:**
- Consumes: задачи 1-4.
- Produces:
  - `store_load(const char *key, void *buf, size_t cap)`, `store_save(const char *key, const void *buf, size_t len)`:
    ключи `"knobs"`, `"custom"`, `"scenes"`, у каждого свой буфер (флаг «грязный» на ключ, задача пишет все грязные).
  - `panel_uart_receive_edit(pl_custom_edit_t *)`, `panel_uart_receive_scene(pl_scene_t *)`;
    `panel_uart_start(s, l, custom, scene, demo_on)`; снимок шлёт `bsp_page = acid_link_page(...)`,
    затем 6 кадров `PL_CUSTOM` и `PL_SCENE_STATE`; `l->custom_due` - кадры отдельных страниц.
  - `main.c`: `static acid_custom_t s_custom; static acid_scene_t s_scene;` загрузка из NVS при старте,
    `acid_link_use_custom` после каждого `acid_link_init`. В `render_block` перед куском: правки (`acid_link_custom_edit`),
    сцены (`store` или `recall`), `acid_scene_step` (1 -> `panel_uart_snapshot_due()`), `acid_link_take` после
    каждого события. Запись `custom` и `scenes` по флагам `save` тем же правилом тишины, и в демо.

Хост-тестов нет (ESP-IDF). Проверка - сборка и железо.

- [ ] **Step 1:** `. ~/esp/esp-idf-v6.1/export.sh && idf.py build`, Expected: собирается без предупреждений.
- [ ] **Step 2:** демо бит в бит с `main` (рецепт `CLAUDE.md`), Expected: `cmp` молчит.
- [ ] **Step 3:** прошить `idf.py -p /dev/ttyACM0 flash`, лог pyserial: старт без ошибок, `knobs restored`, демо играет.
- [ ] **Step 4: Commit** `git commit -m "main: custom pages and scenes storage, panel frames, scene step"`

### Task 7: UI панели

**Files:**
- Create: `panel/main/ui_custom.c`, `panel/main/ui_custom.h`
- Modify: `panel/main/ui.c`, `panel/main/link.c`, `panel/main/CMakeLists.txt`

**Interfaces:**
- Consumes: задачи 4-5. `link.c` шлёт `PL_CUSTOM_EDIT` и `PL_SCENE`.

Требования:
- Шапка меню: кнопка перехода на второй экран меню; на нём в раскладке 2x5 плитки USER 1-6 и SCENES.
- Своя страница - та же сетка 3x5: подпись `pl_slot_label`, значение `user_val` (у псевдонима - `raw` цели),
  крутить как штатную (`PL_SET` с page 10..15). Пустой слот - «-». Кнопка EDIT в шапке.
- EDIT: рисует состояния `pl_edit_t`; PICK_KNOB - сетка выбранной страницы; RANGE - крутилка жестом `pl_gesture`.
- SCENES: 8 плиток (тап - вызов, удержание 600 мс - запись и тост `SAVED`), ручка BARS 0..8 (на экране, шлётся с вызовом),
  подсветка `scene.cur`, у идущего перехода - мигание.
- Отрисовка не блокирует тач (`ui_step`), как в `ui.c`.

- [ ] **Step 1:** `idf.py -C panel build`, Expected: собирается.
- [ ] **Step 2:** прошить панель `idf.py -C panel -p /dev/ttyUSB0 -b 460800 flash` (не прерывать).
- [ ] **Step 3:** проверка пользователем по сценарию спеки (раздел «Проверка», железо).
- [ ] **Step 4: Commit** `git commit -m "panel: custom pages, editor and scenes screens"`

### Task 8: доки, стенд, ревью

**Files:**
- Modify: `README.md`, `docs/controls.html`, `docs/roadmap.md`, `CLAUDE.md` (если меняются цифры бюджета)

- [ ] **Step 1:** README: энкодер 1 до 15, раздел «Свои страницы и сцены», тач-панель; controls.html: `PAGES` и карточка
  своих страниц; roadmap: этап 12.
- [ ] **Step 2:** стенд: временно в стресс-песне вызов сцены переходом на 2 такта каждые 4 такта,
  `idf.py -B build-stress -DESPOSC_STRESS=1 -p /dev/ttyACM0 flash`; худший блок ядра 1 против ~1970 без сцены.
  Если растёт больше 30 мкс - уменьшить `ACID_SCENE_PER_CHUNK`. Откатить стенд, прошить обычную сборку.
- [ ] **Step 3:** ревью ветки свежим агентом, правки по нему.
- [ ] **Step 4: Commit** `git commit -m "docs: custom pages and scenes"`
