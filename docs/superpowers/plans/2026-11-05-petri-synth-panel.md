# Чашка Петри, части 2-3: голос на синте, связь, экран - план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** тип синта 25 PETRI звучит от таблиц чашки: живых с панели или зашитых без неё; страница «Чашка»
(BSP 18) и экран чашки на панели; демо играет PETRI в обеих песнях.

**Architecture:** голос `acid_petri` - чистый C в `components/acid`: 4 голоса читают таблицу волны с кроссфейдом,
общий `acid_ladder_tick`, штатный перегруз слота. Состояние чашки голоса (`acid_petri_t`) есть только у двух синтов,
не у слоёв. Таблица приходит в «почтовый ящик» слота между кусками рендера (фаза событий, оба ядра свободны), слот
забирает её сам в рендере после конца кроссфейда - без локов и гонок. Без таблиц 0.5 с слот сам берёт зашитые
(`acid_petri_tables.c`, сняты хостом библиотекой `components/petri`). Панель держит чашку в своей задаче на ядре 1,
шлёт таблицу четырьмя кадрами `PL_PETRI_0..3`, рисует экран одной картинкой в PSRAM.

**Spec:** `docs/superpowers/specs/2026-11-05-petri-design.md`. Часть 1 (библиотека, стенд) - `2026-11-05-petri-core.md`.

## Global Constraints

- Тип PETRI = 25, `ACID_TYPES` 26, `ACID_LAYER_OFF` 26. Слой не может быть PETRI (значение 25 на странице слоя - игнор).
- Страница «Чашка»: внутренняя 13 (`ACID_PAGE_PETRI`, `PL_PAGE_PETRI`), `ACID_PAGES`/`PL_PAGES` 14, свои страницы
  с 14; на BSP номер 18, значения энкодера 1 больше 18 игнор.
- В рендере (всё из `acid_synth_render*`) без локов, выделений, логов, libm и делений float на сэмпл (`acid_math.h`).
- Демо бит в бит как в `main`, пока в песни не добавлен PETRI (задача 6 меняет звук намеренно).
- Blob ручек `BLOB_MAGIC` версия 4, старая версия 3 импортируется (типы 0-24 как были). Blob сцен - с переходом со
  старых 13 страниц, как уже сделано для прежних.
- Частота 48 кГц, блок 96, кусок 32 сэмпла; события перед каждым куском.
- Комментарии по-русски, код и git по-английски, без длинных тире; git `antifreezzz <me@kvatech.ru>`; ветка `petri`.

## Review Focus

- Таблица приходит посреди кроссфейда и чаще кадра: голос не щёлкает, доигрывает текущий переход, берёт последнюю.
- Смена типа PETRI <-> 303/Plaits посреди ноты: уход/приход 4 мс, голоса PETRI не залипают.
- Панель пропала посреди игры: через 0.5 с голос переходит на зашитые таблицы с кроссфейдом, без тишины.
- Старый blob ручек (версия 3) и старый blob сцен (13 страниц) после прошивки: ручки и сцены на месте.
- Снимок для панели после добавления страницы: оба кадра снимка влезают в 255 байт, панель собирает состояние.

---

### Task 1: Голос `acid_petri`

**Files:** Create `components/acid/acid_petri.c`, `components/acid/include/acid_petri.h`; Test `host/test_petri_voice.c`.

**Interfaces (Produces):**
- `#define ACID_PETRI_VOICES 4`, `ACID_PETRI_TABLE 256`, `ACID_PETRI_LEVELS 4` (копии: исходная, средние по 2, 4, 8)
- `typedef struct { int16_t t[ACID_PETRI_LEVELS][ACID_PETRI_TABLE + 1]; } acid_petri_wave_t;` (+1 - копия точки 0)
- `typedef struct { int16_t tab[ACID_PETRI_TABLE]; int8_t mods[6]; uint8_t jumps; } acid_petri_frame_t;`
- `acid_petri_t`: волны `w[3]` (A, B, F), почтовый ящик `inbox` + флаг `pending`, вес кроссфейда `x` и шаг,
  фриз `m` и цель, голоса (фаза uint32, нота, гейт, огибающая, DC x1/y1, уровень lvl, усиление g, уровень копии),
  сглаженные модуляции, счётчик тишины для зашитых, индекс зашитой таблицы, последний `jumps`.
- `void acid_petri_init(acid_petri_t *p, float sr);`
- `void acid_petri_wave_build(acid_petri_wave_t *w, const int16_t tab[ACID_PETRI_TABLE]);` - копии 2/4/8 средним по кругу
- `void acid_petri_post(acid_petri_t *p, const acid_petri_frame_t *f);` - в ящик (фаза событий)
- `void acid_petri_note_on(acid_petri_t *p, float hz, int vel);` / `void acid_petri_note_off(acid_petri_t *p, float hz);`
  (голос по совпадению частоты, иначе свободный, иначе самый старый; копия таблицы по частоте: <1 кГц 0, <2 кГц 1, <4 кГц 2, выше 3)
- `void acid_petri_set_env(acid_petri_t *p, float attack_s, float release_s);` - коэффициенты при смене ручки
- `void acid_petri_freeze(acid_petri_t *p, int on);` - вкл: F = B; переход 30 мс
- `float acid_petri_osc(acid_petri_t *p);` - сумма голосов за сэмпл (до фильтра), инлайн-друг; раз в 16 сэмплов
  пересчёт усилений голосов `0.3 * acid_rcp(lvl + 0.02)`; вес кроссфейда за 50 мс; при `x >= 1` и `pending` - A = B,
  B = ящик (с копиями), `x = 0`. 0.5 с без ящика - ящик сам наполняет следующая зашитая таблица.
- `int acid_petri_jumped(acid_petri_t *p);` - 1, если с прошлого вызова вырос счётчик прыжков (бьёт огибающую фильтра)
- Зашитые таблицы - `extern const acid_petri_frame_t acid_petri_baked[]; extern const int acid_petri_nbaked;` (задача 2,
  до неё - временная одна синусная таблица в тесте не нужна: голос с пустым набором молчит).

Коэффициенты под 48 кГц: DC `0.9954`, уровень `0.000367`, фриз и сглаживание модуляций - однополюсные от 30 и 15 мс.

- [ ] **Step 1: Failing tests** `host/test_petri_voice.c`:
  - `silent_without_notes`: init, пост таблицы синуса, 4800 сэмплов - все 0.
  - `plays_table_pitch`: синус в таблице, нота 220 Гц, 0.2 с - число переходов через ноль ~ 2 * 220 * 0.2 (±3).
  - `crossfade_no_jump`: таблица +синус, нота, потом таблица -синус посреди: соседние сэмплы отличаются меньше чем на 0.05
    (без скачка), через 60 мс сигнал в противофазе к началу.
  - `latest_table_wins`: два поста подряд до конца кроссфейда - в итоге звучит последний.
  - `four_voices_then_steal`: 5 нот - звучат 4, первая украдена; note_off снимает гейт своего голоса.
  - `mip_by_pitch`: нота 3 кГц берёт копию 2, 300 Гц - копию 0.
  - `wave_build_averages`: меандр -> копия 1 в точке перехода - среднее соседей.
  - `freeze_holds`: фриз, потом пост другой таблицы - через 100 мс звучит прежняя волна.
  - `baked_after_silence`: без постов 0.5 с при зашитом наборе (тестовый набор из 2 таблиц через
    `acid_petri_use_baked(p, frames, n)`) голос звучит.
  - `dc_removed`: таблица с постоянным сдвигом, 1 с - среднее последних 0.1 с близко к 0.
- [ ] **Step 2: Run** `cmake --build host/build` - FAIL (нет функций).
- [ ] **Step 3: Implement.** Добавить в интерфейс `void acid_petri_use_baked(acid_petri_t *p, const acid_petri_frame_t *f, int n);`.
- [ ] **Step 4: Run** `ctest --test-dir host/build` - всё PASS.
- [ ] **Step 5: Commit** `feat(acid): petri wavetable voice`

### Task 2: Зашитые таблицы

**Files:** Create `tools/petri_bake/petri_bake.c` (хост, собирается в `host/CMakeLists.txt` целью `petri_bake`),
`components/acid/acid_petri_tables.c` (сгенерирован, в git); Test `host/test_petri_baked.c`.

`petri_bake` гонит чашку `components/petri` seed 1, Митоз, 600 шагов разгона, дальше кадры по 4 шага; каждые 10 кадров
(0.5 с) снимает таблицу: 32 таблицы, режимы и формы по кругу (Высота/Круг, Фолд/Роза, Край/Восьмёрка, Пустота/Эпицикл),
контраст 0.3. Пишет C-файл `const acid_petri_frame_t acid_petri_baked[32] = {...}; const int acid_petri_nbaked = 32;`.

- [ ] **Step 1: Failing test** `baked_tables_alive`: 32 таблицы, у каждой пик больше 2000, среднее по модулю меньше 50,
  соседние таблицы различаются.
- [ ] **Step 2: Run** - FAIL (нет символа). **Step 3:** написать `petri_bake`, сгенерировать файл, голос по умолчанию
  берёт `acid_petri_baked`. **Step 4:** PASS. **Step 5: Commit** `feat(acid): baked petri tables`

### Task 3: Тип 25 в синте

**Files:** Modify `components/acid/include/acid_synth.h`, `components/acid/acid_synth.c`, `components/panel_link/pl_labels.c`
(TYPES: "PETRI" на 25, "OFF" на 26; подписи страницы синта типа PETRI); Test `host/test_petri_slot.c`, правки старых
тестов, где 25 = выкл.

**Interfaces:** `acid_slot_t` получает `acid_petri_t *pet` (NULL у слоёв); `acid_synth_t` - `acid_petri_t petri[ACID_SLOTS]`.
`void acid_synth_petri(acid_synth_t *s, const acid_petri_frame_t *f);` - пост в оба синта (фаза событий).
`#define ACID_TYPE_PETRI 25`.

Ручки синта типа PETRI (индекс энкодера с 0): 2 срез `expo(60, 8000)`, 3 резонанс `lin(0, 0.97)`, 4 глубина огибающей
(0-6 октав), 5 спад `expo(0.03, 2)`, 6 атака `0.001 * 2000^x`, 7 релиз `expo(0.02, 3)`, 8 режим 0-3, 9 контраст,
10 форма 0-4, 11 радиус, 12 центр; 13-15 перегруз (общий). Списки 8 и 10 - вне списка игнор. Умолчания RAW:
срез 70 (~540 Гц), рез 79 (0.6), огибающая 64, спад 64, атака 38 (~10 мс), релиз 75, режим 0, контраст 0, форма 0,
радиус 50 (18 клеток), центр 17 (дрейф 2).
Рендер: голос -> `acid_ladder_tick` (коэффициент раз в 16 сэмплов: срез * 2^(огиб * fenv * 6 + маршрут), маршруты из
модуляций и глубин страницы «Чашка») -> перегруз -> громкость. Огибающая фильтра бьёт на ноте и на прыжке.
Ноты: `note_on`/`note_release` слота зовут `acid_petri_note_on/off` с частотой `slot_note` (тональность, без глайда).
Blob: магия 4, импорт версии 3 (типы 0-24 по местам, ручки типа 25 не заданы).

- [ ] **Step 1: Failing tests:** `petri_type_sounds` (тип 25, нота - звук), `layer_refuses_petri` (страница слоя, 25 -
  тип не меняется), `layer_off_is_26`, `blob_v3_imports`, `petri_knob_lists` (режим 7 - игнор),
  `petri_route_moves_cutoff` (модуляция pop = 127 при глубине -0.6 - коэффициент фильтра ниже).
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4:** весь `ctest` PASS, демо бит в бит как в `main` (обе песни).
- [ ] **Step 5: Commit** `feat(acid): petri synth type`

### Task 4: Страница «Чашка» и снимок в два кадра

**Files:** `acid_synth.h/.c` (`ACID_PAGES` 14, `ACID_PAGE_PETRI` 13, BSP 18), `acid_scene.c` (переход с 13 страниц),
`pl_proto.h/.c` (`PL_PAGES` 14, `PL_PAGE_PETRI` 13, `PL_USER_FIRST` 14, `pl_snapshot_t` без `bsp_pos`/`held`, новый
`PL_SNAPSHOT2` = `bsp_pos[16]` + `held[(PL_PAGES*16)/8]`), `pl_model.c`, `main/panel_uart.c`, `panel/main/link.c`,
`pl_labels.c` (страница PETRI: имя "PETRI", подписи, списки), `acid_link.c` при нужде; тесты `test_proto`, `test_model`,
`test_scene`, `test_synth` и новые.

Ручки «Чашки» (энкодер с 1): 2 пресет 0-5, 3 F тонко, 4 k тонко (64 = пресет), 5 шагов 0-6, 6-9 численность
пожирателей/сеятелей/мутагенов/вихрей (0-16/0-4/0-4/0-3), 10 аппетит (0-0.5), 11 сила мутагенов (0-3), 12-14 глубины
(64 = 0, ±1): пожиратели->срез, мутаген->резонанс, вихрь+прыжок->огибающая, 15 фриз (64+ вкл), 16 пересеять (0 -> 1).
Умолчания RAW: 0, 64, 64, 4, 3, 1, 2, 1, 38, 42, 26, 108, 102, 0, 0. Синт использует 12-15, остальное - панель.

- [ ] **Step 1: Failing tests:** `snapshot_fits` (`sizeof(pl_snapshot_t) <= 255`, `PL_SNAPSHOT2` тоже), `bsp_18_is_petri`,
  `bsp_19_ignored`, `scene_imports_13_pages`, `petri_freeze_knob` (E15 128 -> фриз голоса), `model_takes_snapshot2`.
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4:** весь `ctest` PASS, демо бит в бит как в `main`.
- [ ] **Step 5: Commit** `feat: petri dish knob page`

### Task 5: Кадры таблицы панель -> синт

**Files:** `pl_proto.h/.c` (`PL_PETRI_0..3`, `pl_petri_part_t`, сборщик `pl_petri_rx_t`), `main/panel_uart.c` (приём,
очередь последней таблицы), `main/main.c` (в фазе событий `acid_synth_petri`); тест `host/test_petri_proto.c`.

**Interfaces:** `void pl_petri_split(const int16_t tab[256], const int8_t mods[6], uint8_t jumps, uint8_t seq, uint8_t out[4][137], size_t len[4]);`
(готовые нагрузки 129/129/129/137), `int pl_petri_collect(pl_petri_rx_t *r, uint8_t type, const uint8_t *payload, int16_t tab[256], int8_t mods[6], uint8_t *jumps);`
- 1, когда собраны все 4 части одного номера.

- [ ] **Step 1: Failing tests:** `split_collect_roundtrip`, `mixed_seq_rejected` (части разных номеров - не собрано),
  `lost_part_waits` (без части 2 - не собрано, следующий номер целиком - собран), `frame_lengths_fixed` (чужая длина -
  `pl_rx` отбрасывает).
- [ ] **Step 2-4:** FAIL, implement, PASS. **Step 5: Commit** `feat(link): petri wave frames`

### Task 6: Демо и смоук

**Files:** `acid_metal_song.c`, `acid_demo_song.c` (PETRI на синте 2 в одной части каждой песни: тип 25, страница 18:
численность и глубины, фриз вкл/выкл), `host/test_demo_smoke.c` (`z.petri`: тип 25 звучал, ручка страницы 18 крутилась),
`host/test_stress.c`/`acid_stress_song.c` при нужде (PETRI вместо modal на синте 1 в стресс-песне - отдельной частью).

- [ ] **Step 1: Failing test** - охват `petri` в обеих песнях. **Step 2:** FAIL. **Step 3:** части в песнях. **Step 4:** PASS,
  WAV обеих песен на слух не проверить - уровень PETRI: пик части не громче соседних (rms в тесте). **Step 5: Commit** `feat(demo): petri part in both songs`

### Task 7: Чашка на панели

**Files:** Create `panel/main/petri_task.c/.h` (задача на ядре 1, приоритет 1; чашка выделяется при первом PETRI и
не освобождается; ручки из модели: страница синта с типом 25 (синт 1 главнее) и «Чашка»; пересев по переходу E16;
капли из очереди тапов; кадр: `petri_frame`, `petri_capture`, `pl_petri_split`, отправка), Modify `panel/main/link.c/.h`
(`link_petri_send`), `panel/main/main.c`; стенд `petri_bench.c` переходит на эту задачу или остаётся флагом.

- [ ] **Step 1:** хост-тест на чистую логику выбора ручек: `pl_petri_params(const uint8_t raw[PL_PAGES][16], petri_params_t *out)`
  в `components/panel_link/pl_petri.c` (какой синт PETRI, перевод сырых значений в параметры чашки) - `params_from_synth1`,
  `params_none_when_no_petri`, `reseed_edge`. **Step 2-4:** FAIL, implement, PASS + сборка панели без предупреждений.
- [ ] **Step 5: Commit** `feat(panel): petri dish task`

### Task 8: Экран чашки

**Files:** Create `panel/main/ui_petri.c`; Modify `panel/main/ui.c` (плитка PETRI во втором экране меню, страница 13
в листании, открытие экрана), `panel/main/ui_priv.h`. Картинка 384x384 RGB565 в PSRAM (`lv_image`), заполняется раз в
кадр: V по LUT 5 опорных точек, след Δk, точки орбиты, жители квадратиками. Тап/ведение - капля в очередь задачи.
Шапка: назад, «KNOBS» - страница 13.

- [ ] **Step 1:** хост-тест LUT и раскраски клетки (`pl_petri_color(v, dk)` в `pl_petri.c`): концы палитры, след мутагена.
- [ ] **Step 2-4:** FAIL, implement, PASS; сборка; прошивка панели; лог `lvgl heap` и internal RAM после открытия экрана.
- [ ] **Step 5: Commit** `feat(panel): petri dish screen`

### Task 9: Доки и железо

**Files:** `README.md` (тип 25, страница 18, LAYER_OFF 26, экран), `docs/controls.html` (`PAGES`, `MODELS`, `K303`-подобная
таблица PETRI, карточки), `CLAUDE.md` (бюджет PETRI), спека (замеры).

- [ ] **Step 1:** стенд `build-stress` с PETRI: ядро 1 и 0 в норме, защита. **Step 2:** обычная прошивка синта и панели,
  лог: таблицы идут, нет потерь CRC, fps панели. **Step 3:** доки. **Step 4: Commit** `docs: petri dish`.
