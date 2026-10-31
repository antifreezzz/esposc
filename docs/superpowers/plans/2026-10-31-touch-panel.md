# Stage 11: тач-панель, план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** панель Guition 4848S040 показывает и крутит все ручки синта по UART, вместе с BSP и с демо.

**Architecture:** синт главный: хранит зеркало сырых значений всех ручек, применяет правки панели к любой
странице, не трогая страницу BSP, делает подхват и глушит CC демо после правки с панели. Общий чистый C
(`components/panel_link`): кадры протокола, подписи ручек, жест вращения, модель состояния панели; его
собирают обе прошивки и хост-тесты. Прошивка панели - отдельный проект IDF `panel/` на LVGL.

**Tech Stack:** C11, ESP-IDF 6.1, LVGL 9 через `espressif/esp_lvgl_port`, `espressif/esp_lcd_st7701`
(RGB + 3-wire SPI init), `espressif/esp_lcd_touch_gt911`; хост-тесты `host/test.h`.

**Spec:** `docs/superpowers/specs/2026-10-31-touch-panel-design.md`

## Global Constraints

- UART1 **1 Мбод**, 8N1, без управления потоком. Синт: IO17 TX, IO18 RX. Панель: IO40 RX, IO2 TX.
- Панель: RGB по пинам и таймингам из `~/orca/projects/minimaxdumbtest/firmware/src/panel.cpp` (de 18, vsync 17,
  hsync 16, pclk 21; R 11,12,13,14,0; G 8,20,3,46,9,10; B 4,5,6,7,15; hsync 1/10/8/50, vsync 1/10/8/20, pclk 12 МГц),
  init ST7701 оттуда же дословно (3-wire SPI: CS 39, SCK 48, MOSI 47), подсветка IO38, GT911 I2C SDA 19, SCL 45, адрес 0x5D,
  координаты уже в ориентации экрана. Модуль N16R8: флэш 16 МБ, PSRAM octal 8 МБ.
- В рендере (всё из `render_block` и `acid_synth_render*`) ни локов, ни аллокаций, ни логов; очереди FreeRTOS
  только с таймаутом 0 (как `midi_in_receive`).
- Демо бит в бит как в `main`, пока панель ничего не правила (процедура `cmp` из `CLAUDE.md`).
- Blob памяти ручек не меняется (`BLOB_MAGIC` не трогать): зеркало в blob не идёт.
- Подписи на экране по-английски, капителью. Комментарии и доки по-русски, без длинных тире. Git `antifreezzz <me@kvatech.ru>`.
- TDD: хост-тест до кода, увидеть красный. Коммит после каждой задачи.
- Разъём USB DevKit в ПК не втыкать: синт через `/dev/ttyACM0`, панель через свой USB-C `/dev/ttyUSB0`.

## Review Focus

1. Перезагрузка панели посреди игры: панель просит снимок и показывает то, что звучит, а не нули.
   Тест: `test_model_snapshot_replaces_all` (задача 5).
2. Смена типа синта (энкодер 2 на странице 6/7) с панели или BSP: остальные 14 ручек страницы приходят новыми
   (у каждого типа своя память). Тест: `test_type_change_sends_snapshot` (задача 2).
3. BSP пропускает значения при быстром вращении (шаг 4+): подхват ловит пересечение, а не равенство.
   Тест: `test_pickup_skip_over` (задача 2).
4. Мусор и обрыв кадра на линии (перезагрузка платы, открытие порта): приёмник выбрасывает и ловит следующий
   кадр. Тест: `test_garbage_then_frame`, `test_truncated_frame` (задача 3).
5. Палец проходит через ±180° (6 часов) и через центр экрана: значение не прыгает. Тесты: `test_turn_wraps_180`,
   `test_turn_dead_center` (задача 4).

---

### Task 1: зеркало ручек и правка любой страницы в синте

**Files:**
- Modify: `components/acid/include/acid_synth.h`, `components/acid/acid_synth.c` (`control_change`, `acid_synth_init`)
- Test: `host/test_knob_mirror.c`

**Interfaces:**
- Produces:
  - `#define ACID_PAGES 10`
  - `void acid_synth_set_knob(acid_synth_t *s, int page, int enc, int v);` энкодер 0-based как `slot` в
    `control_change` (0 = энкодер 1, 1..15 = энкодеры 2..16), `enc == 0` и `page` вне 0..9 игнорирует, `s->page` не меняет.
  - `int acid_synth_knob(const acid_synth_t *s, int page, int enc);` 0..127 сейчас действующее значение; у синтов
    из `knob[sel_type][enc]`, нетронутое (0xFF) - умолчание типа.
  - поле `uint8_t raw[ACID_PAGES][ACID_KNOBS]` в `acid_synth_t` для страниц 0-5, 8, 9.

- [ ] **Step 1: тесты.**
  - `test_set_knob_keeps_page`: init, `acid_synth_set_knob(s, 0, 1, 30)` → `s->page == 6`, `acid_synth_knob(s, 0, 1) == 30`.
  - `test_set_knob_equals_cc`: два синта; в одном CC страницы 8 и CC энкодера 4 = 100 через `acid_synth_midi`,
    в другом `acid_synth_set_knob(s, 8, 3, 100)` (страница остаётся 6); 1 с рендера одинаковых нот - буферы равны.
  - `test_mirror_follows_cc`: на каждой странице 0..9 CC каждого энкодера 2..16 значением 77 → `acid_synth_knob == 77`
    (для списков, где 77 вне диапазона и игнорируется синтом, `acid_synth_knob` остаётся прежним).
  - `test_mirror_synth_type`: страница 6, энкодер 2 = 0 (303), энкодер 3 = 10; энкодер 2 = 11 (FM 2-op), энкодер 3 = 90;
    энкодер 2 = 0 → `acid_synth_knob(s, 6, 2) == 10`.
  - `test_defaults_match_init`: свежий init против init + `acid_synth_set_knob(page, enc, acid_synth_knob(init, page, enc))`
    по всем страницам кроме 6-7 энкодера 2; 2 с одинаковой партии (ноты на каналах 2, 5, 6) - rms разницы ниже -40 dBFS.
    Точное равенство значений для ручек, у которых умолчание уже задано CC: панорамы `PAN_DEFAULT`, тарелки
    (`CYM_KNOB`, хвост 127, строй и панорама 64), `dl_fb` 53, `dl_tone` 83, тональность, перкуссия.
- [ ] **Step 2:** `ctest --test-dir host/build -R knob_mirror --output-on-failure` → FAIL (нет символов).
- [ ] **Step 3:** вынести тело `control_change` после разбора `slot` в `apply_page(s, page, slot, v)`; `control_change`
  зовёт её с `s->page`, `acid_synth_set_knob` - с переданной страницей. `raw` пишется в `apply_page` только когда
  синт значение принял. Умолчания `raw` - таблица в `acid_synth_init`: обратные к `lin`/`expo` от умолчаний,
  округлённые до CC (посчитать один раз скриптом, вписать числами с комментарием).
- [ ] **Step 4:** все хост-тесты зелёные; инвариант демо `cmp` с `main` проходит.
- [ ] **Step 5:** коммит `acid: knob mirror and per-page knob set for the panel`.

### Task 2: связка синта с панелью: подхват, глушение демо, события

**Files:**
- Create: `components/acid/include/acid_link.h`, `components/acid/acid_link.c`
- Test: `host/test_link.c`

**Interfaces:**
- Consumes: `acid_synth_set_knob`, `acid_synth_knob`, `acid_synth_midi`, `acid_bsp_enc_cc`.
- Produces:
  ```c
  enum { ACID_SRC_BSP, ACID_SRC_DEMO, ACID_SRC_PANEL };
  typedef struct { uint8_t page, enc, value, src, bsp_pos, held; } acid_link_ev_t; // held: 1 - ждёт подхвата BSP
  typedef struct {
      uint8_t bsp_pos[ACID_KNOBS];          // последнее значение энкодера BSP, 0xFF - не крутили
      uint8_t held[ACID_PAGES][ACID_KNOBS]; // ручка правлена с панели, BSP молчит до пересечения
      int demo_muted;                       // панель правила в демо: CC демо больше не идут
      int snapshot_due;                     // сменился тип синта: отправить снимок
  } acid_link_t;
  void acid_link_init(acid_link_t *l);  // и при каждой смене режима демо <-> live
  // Событие BSP или демо в синт через фильтр; в out - что сообщить панели (0 или 1 событие). Возвращает число событий.
  int acid_link_midi(acid_link_t *l, acid_synth_t *s, const acid_midi_event_t *ev, int src, acid_link_ev_t *out);
  int acid_link_panel_set(acid_link_t *l, acid_synth_t *s, int page, int enc, int v, acid_link_ev_t *out);
  ```
  `acid_link_midi` с `src == ACID_SRC_DEMO` и `demo_muted` выбрасывает CC, остальное пропускает. Смена страницы BSP
  (энкодер 1) даёт событие с `enc = 0`, `value = page`.

- [ ] **Step 1: тесты.**
  - `test_pickup_blocks_until_cross`: BSP энкодер 3 = 87 на странице 6; панель ставит (6, 2, 30); BSP 86, 60, 31
    → `acid_synth_knob(s,6,2) == 30`, у событий `held == 1`; BSP 29 → значение 29, `held == 0`, следующие CC проходят.
  - `test_pickup_skip_over`: то же, BSP идёт 40 → 24 (через 30 скачком) → подхвачено, значение 24.
  - `test_pickup_from_below`: BSP 10, панель 30, BSP 14, 22, 33 → подхват на 33.
  - `test_pickup_per_page`: метка на (6, 2) не мешает энкодеру 3 на странице 7.
  - `test_pickup_unknown_bsp_pos`: BSP энкодер ещё не крутили (0xFF), панель ставит 30, первый CC BSP 90 только
    запоминает позицию (не применяется), следующий 20 пересекает → применяется.
  - `test_demo_muted_after_panel`: демо шлёт CC → применяется; `acid_link_panel_set` → дальше CC демо не меняют
    `acid_synth_knob`, ноты и клок проходят; `acid_link_init` снимает глушение.
  - `test_type_change_sends_snapshot`: CC энкодера 2 на странице 6 (из BSP и из панели) ставит `snapshot_due`.
  - `test_page_event`: CC энкодера 1 = 3 → событие `{enc 0, value 3, src BSP}`, `s->page == 3`.
- [ ] **Step 2:** `ctest -R test_link` → FAIL.
- [ ] **Step 3:** реализовать в `acid_link.c`; правило пересечения: `(prev - target) * (v - target) <= 0`.
- [ ] **Step 4:** все хост-тесты зелёные.
- [ ] **Step 5:** коммит `acid: panel link with BSP pickup and demo knob mute`.

### Task 3: протокол кадров

**Files:**
- Create: `components/panel_link/CMakeLists.txt` (компонент IDF без зависимостей), `components/panel_link/include/pl_proto.h`, `components/panel_link/pl_proto.c`
- Modify: `host/CMakeLists.txt` (собрать `components/panel_link/*.c` в каждый тест, include)
- Test: `host/test_proto.c`

**Interfaces:**
- Produces:
  - Кадр: `0xA5, type, len, payload[len], crc8` (полином 0x07 по type, len, payload), `len <= 255`.
  - Типы: `PL_SNAPSHOT` (синт→панель: `mode` 0 демо / 1 live, `bsp_page`, `raw[10][16]`, `bsp_pos[16]`, `held[10][16]` битами);
    `PL_KNOB` (синт→панель: `acid_link_ev_t` 6 байт); `PL_SET` (панель→синт: page, enc, value); `PL_HELLO` (панель→синт: просит снимок).
  - `size_t pl_encode(uint8_t type, const void *payload, size_t len, uint8_t *out);` возвращает длину кадра.
  - `typedef struct { ... } pl_rx_t; void pl_rx_init(pl_rx_t *r);`
  - `int pl_rx_feed(pl_rx_t *r, uint8_t byte, uint8_t *type, const uint8_t **payload, size_t *len);` 1 - кадр собран.
  - Структуры полезной нагрузки `pl_snapshot_t`, `pl_knob_t`, `pl_set_t` упакованы побайтно, без зависимости от `acid_*`.
- [ ] **Step 1: тесты** `test_roundtrip` (каждый тип), `test_bad_crc_dropped`, `test_garbage_then_frame` (мусор, в том числе
  0xA5 внутри мусора, потом верный кадр - собран ровно он), `test_truncated_frame` (оборванный кадр, затем целый - собран целый),
  `test_back_to_back` (два кадра подряд в одном буфере).
- [ ] **Step 2:** FAIL. **Step 3:** реализовать. **Step 4:** зелёные.
- [ ] **Step 5:** коммит `panel_link: framed UART protocol`.

### Task 4: подписи ручек и жест

**Files:**
- Create: `components/panel_link/include/pl_labels.h`, `pl_labels.c`, `include/pl_gesture.h`, `pl_gesture.c`
- Test: `host/test_labels.c`, `host/test_gesture.c`

**Interfaces:**
- Produces:
  - `const char *pl_page_name(int page);`
  - `const char *pl_knob_label(int page, int enc, const uint8_t page_raw[16]);` NULL - пустой слот; `page_raw` нужен для
    типа синта (энкодер 2 стр. 6-7) и модели перкуссии (энкодер 2 стр. 3-5).
  - `void pl_knob_range(int page, int enc, const uint8_t page_raw[16], int *lo, int *hi);` 0..127 или границы списка.
  - `const char *pl_value_text(int page, int enc, int v, const uint8_t page_raw[16], char buf[12]);` слово для списков, иначе число.
  - `int pl_sector(int x, int y);` 0..14 (энкодер 2..16, по часовой от 12 часов, 24° на сектор), -1 ближе 40 px к центру (240, 240).
  - `typedef struct {...} pl_turn_t; void pl_turn_begin(pl_turn_t *t, int x, int y, int v, int lo, int hi);`
    `int pl_turn_move(pl_turn_t *t, int x, int y);` новое значение: полный оборот = `hi - lo + 1` шагов,
    приращение угла с разворотом через ±180°, упор в lo/hi без накопления, ближе 40 px к центру приращения нет.
  - `typedef struct {...} pl_press_t;` + `int pl_press_step(pl_press_t *p, int down, int x, int y, uint32_t ms);`
    возвращает `PL_PRESS_NONE`, `PL_PRESS_TAP`, `PL_PRESS_HOLD` (300 мс без сдвига больше 12 px), `PL_PRESS_RELEASE` (отпущен после HOLD).
- Подписи (черновик для вычитки пользователем; список значений в скобках):
  - Страницы: MIX, DRUMS, CYMBALS, TOMS, RIM, COWBELL, SYNTH 1, SYNTH 2, FX, TONAL.
  - MIX: MASTER, DRIVE, SYNTH 1, SYNTH 2, KICK, HATS, CLAP, SNARE, PUMP, PUMP REL, DJ FILTER, DJ RESO, PAN S1, PAN S2, -.
  - DRUMS: KICK TUNE, KICK SWEEP, SWEEP TIME, KICK LEN, HAT TONE, CH LEN, OH LEN, CLAP TONE, CLAP TAIL, SN TUNE, SN SNAP, SN LEN, PAN HAT, PAN CLAP, PAN SN.
  - CYMBALS: CRASH VOL, CRASH TAIL, CRASH TUNE, CRASH PAN, RIDE VOL, RIDE TAIL, RIDE TUNE, RIDE PAN, BELL VOL, VEL SENS, остальное пусто.
  - TOMS/RIM/COWBELL: MODEL (ANALOG, SQUARES, FM, MODAL, NOISE), TUNE, DECAY, P DEPTH, P TIME, TONE, CHAR, CLICK, DRIVE, CRUSH,
    VOLUME, PAN, VEL SENS, у томов SPREAD и WIDTH (у рима и ковбелла пусто). TONE/CHAR по модели: ANALOG SHAPE/OVERTONE,
    SQUARES BP FREQ/RATIO, FM INDEX/RATIO, MODAL BRIGHT/MODES, NOISE RESO/COLOR.
  - SYNTH 303: TYPE, CUTOFF, RESO, ENV, DECAY, ACCENT, WAVE (SAW, SQR), GLIDE, TRANS, -, -, -, DRIVE, TONE, HOWL.
  - SYNTH Plaits: TYPE, HARMONICS, TIMBRE, MORPH, DECAY, LPG, GLIDE, TRANS, T ENV, M ENV, P ENV, ACCENT, DRIVE, TONE, HOWL.
  - TYPE (0..24): 303, VA VCF, PHASE DIST, DX7 1, DX7 2, DX7 3, TERRAIN, STRINGS, CHIP, VA 2WAVE, WAVESHAPE, FM 2OP, FORMANT,
    ADDITIVE, WAVETABLE, CHORDS, SPEECH, SWARM, NOISE, PARTICLE, STRING, MODAL, BASS DRUM, SNARE, HI-HAT
    (порядок как `MODELS` в `docs/controls.html`, сверить с `acid_plaits`).
  - FX: CRUSH BITS, CRUSH RATE, DLY S1, DLY S2, DLY DRUMS, DLY TIME (1/16, 1/8, 3/16, 1/4, 3/8), DLY FB, DLY TONE,
    STUTTER (OFF, 1/4, 1/8, 1/16, 1/32), MOTION (CENTER, PING-PONG, RANDOM), WIDTH, остальное пусто.
  - TONAL: STEP LEN (OFF, 1..16), STEPS (1..8), ORDER (FWD, BOUNCE, RANDOM), TARGET (BOTH, S1, S2), HELD (FINISH, SLIDE, JUMP),
    SCALE (CHROM, MAJOR, MINOR, DORIAN, PHRYG, MIXO, HARM MIN, PENTA), ROOT (C .. B), STEP 1 .. STEP 8.
- [ ] **Step 1: тесты.** `test_labels_cover_synth`: для каждой страницы, каждого типа синта и модели перкуссии - NULL только
  у пустых слотов из списка выше, длина подписи не больше 10 символов; `test_ranges`: (8, 5) → 0..4, (9, 1) → 1..8,
  (6, 1) → 0..24, (3, 1) → 0..4, (0, 1) → 0..127; `test_value_text`: (8, 8, 2) → "1/8", (6, 6, 1) при 303 → "SQR".
  Жест: `test_sector_clock` (точка над центром → 0, справа → сектор 3 или 4 по границе 90°, -1 в центре),
  `test_turn_full_circle` (оборот по часовой от 0 → 127, против → 0), `test_turn_wraps_180`, `test_turn_dead_center`,
  `test_turn_clamp_and_back` (за упор 127 и обратно на 10° → сразу меньше 127), `test_turn_list` (5 значений: 72° на шаг),
  `test_press_hold`, `test_press_moved_is_not_hold`, `test_press_tap`.
- [ ] **Step 2:** FAIL. **Step 3:** реализовать. **Step 4:** зелёные.
- [ ] **Step 5:** коммит `panel_link: knob labels and rotary touch gesture`.

### Task 5: модель состояния панели

**Files:**
- Create: `components/panel_link/include/pl_model.h`, `pl_model.c`
- Test: `host/test_model.c`

**Interfaces:**
- Consumes: типы `pl_proto.h`.
- Produces:
  ```c
  typedef struct {
      int have;                 // снимок пришёл
      int mode, bsp_page;
      uint8_t raw[10][16], bsp_pos[16], held[10][16];
      uint32_t last_rx_ms;
      int toast_page, toast_enc; uint32_t toast_until_ms; // всплывашка BSP, 1500 мс
  } pl_model_t;
  void pl_model_init(pl_model_t *m);
  void pl_model_apply(pl_model_t *m, uint8_t type, const uint8_t *payload, size_t len, uint32_t ms);
  int pl_model_need_hello(const pl_model_t *m, uint32_t ms); // нет снимка или тишина больше 2 с
  void pl_model_local_set(pl_model_t *m, int page, int enc, int v); // сразу показать своё, не ждать эха
  ```
- [ ] **Step 1: тесты** `test_model_snapshot_replaces_all`, `test_knob_updates_value`, `test_toast_only_from_bsp` (демо и панель
  всплывашку не ставят), `test_page_event` (enc 0 меняет `bsp_page`), `test_need_hello` (до снимка и после 2 с тишины).
- [ ] **Step 2-4:** FAIL, реализовать, зелёные. **Step 5:** коммит `panel_link: panel state model`.

### Task 6: UART в прошивке синта

**Files:**
- Create: `main/panel_uart.h`, `main/panel_uart.c`
- Modify: `main/main.c` (`render_block`, `enter_mode`, старт задачи), `main/CMakeLists.txt` (REQUIRES `panel_link`, `esp_driver_uart`)

**Interfaces:**
- Consumes: `acid_link_*`, `pl_encode`, `pl_rx_*`, `acid_synth_knob`.
- Produces: `esp_err_t panel_uart_start(acid_synth_t *s, acid_link_t *l);` `int panel_uart_receive(pl_set_t *out);`
  (таймаут 0, из `render_block`); `void panel_uart_send_knob(const acid_link_ev_t *ev);` (таймаут 0, при переполнении
  событие теряется: следующий снимок исправит); `void panel_uart_snapshot_due(void);`

- [ ] **Step 1:** в `render_block` все события BSP и демо идут через `acid_link_midi`, правки панели - через
  `acid_link_panel_set`; события в `panel_uart_send_knob`. `enter_mode` зовёт `acid_link_init` и `panel_uart_snapshot_due`.
  Снимок собирает задача UART (ядро 0, приоритет ниже `slot1`), читая `s_synth` без лока: гонка даёт устаревшую
  цифру на экране до следующего события, звук не трогает (*решение агента*, записать комментарием).
- [ ] **Step 2:** хост-тесты зелёные, `idf.py build` без предупреждений; инвариант демо `cmp` с `main`.
- [ ] **Step 3:** на плате: прошить, демо играет, в логе нет underrun; стенд `build-stress` - ядро 1 и ядро 0 не хуже
  цифр из `CLAUDE.md` (~1630/2000 и ~440/666). Прошить обычную сборку обратно.
- [ ] **Step 4:** коммит `main: panel UART link at 1 Mbaud`.

### Task 7: прошивка панели: экран, тач, связь

**Files:**
- Create: `panel/CMakeLists.txt` (`EXTRA_COMPONENT_DIRS ../components/panel_link`), `panel/sdkconfig.defaults`,
  `panel/main/idf_component.yml`, `panel/main/main.c`, `panel/main/display.c/.h`, `panel/main/link.c/.h`, `panel/main/ui.c/.h`

**Interfaces:**
- Consumes: `pl_proto`, `pl_model`, `pl_labels`, `pl_gesture`.

- [ ] **Step 1: экран и тач.** `display_start()` поднимает ST7701 (init дословно из reference в `vendor_config`), RGB
  с буферами кадра в PSRAM, GT911, подсветку, `esp_lvgl_port`. `sdkconfig.defaults`: `CONFIG_IDF_TARGET="esp32s3"`,
  16 МБ флэш, `CONFIG_SPIRAM_MODE_OCT=y`, 240 МГц, лог на UART0 (CH340). Проверка: тестовый экран с кругом и точкой
  под пальцем, пользователь смотрит; координаты касаний в логе совпадают с углами экрана.
- [ ] **Step 2: связь.** `link_start()`: UART1 IO2 TX / IO40 RX 1 Мбод, задача приёма кормит `pl_model_apply`,
  по `pl_model_need_hello` шлёт `PL_HELLO`, `link_set(page, enc, v)` шлёт `PL_SET`. Проверка: в логе панели снимок
  и события от демо синта.
- [ ] **Step 3: UI** по одобренным макетам (`.superpowers/brainstorm/*/content/ring-v2.html`): меню страниц (обведена `bsp_page`),
  круг ручек с мини-дугами, захват по `PL_PRESS_HOLD` в секторе `pl_sector`, вращение `pl_turn_move` → `pl_model_local_set`
  + `link_set` (не чаще раза в 10 мс на ручку), голубое кольцо `bsp_pos` при `held`, верхняя полоса 40 px - назад,
  всплывашка от `toast_*`. Перерисовка только изменённых объектов LVGL.
- [ ] **Step 4:** коммит `panel: LVGL touch UI for the synth`.

### Task 8: проверка на столе и документация

**Files:**
- Modify: `README.md` (раздел панели: провода, прошивка `idf.py -C panel -p /dev/ttyUSB0 flash`, поведение), `docs/controls.html`
  (карточка про панель), `CLAUDE.md` (команда прошивки панели), `tools/uartlink/README.md` (ссылка, что связь теперь в прошивке)

- [ ] **Step 1:** сценарии пользователя: (1) демо без BSP, правка с панели слышна, после неё демо ручки не крутит, ноты играют;
  (2) BSP, панель показывает и всплывает; (3) BSP и тач вместе, подхват без прыжков; плюс перезагрузка панели посреди игры.
  Пользователь подтверждает вживую.
- [ ] **Step 2:** обновить доки, коммит `docs: touch panel`.
- [ ] **Step 3:** обновить память проекта (`ui-panel.md`).
