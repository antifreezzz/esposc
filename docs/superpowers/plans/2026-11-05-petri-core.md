# Чашка Петри, часть 1: библиотека чашки и стенд панели - план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** чистый C компонент `components/petri` (Грей-Скотт Q15 на месте, жители, снятие таблицы и модуляций) с
хост-тестами и стенд на панели, который меряет мс на шаг и свободную внутреннюю RAM.

**Architecture:** одна структура `petri_t` со всей чашкой (сетка, Δk, жители, орбита), без выделений памяти.
Сетка считается целыми числами на месте с копиями двух строк. Жители и снятие - float, раз в кадр. Компонент
собирают хост-тесты (`host/CMakeLists.txt`) и панель (`EXTRA_COMPONENT_DIRS`); синт подключит его в части 2.

**Tech Stack:** C11, ESP-IDF 6.1, хост-тесты на `host/test.h`.

**Spec:** `docs/superpowers/specs/2026-11-05-petri-design.md`

Часть 2 (отдельный план после стенда): голос и тип 25 на синте, зашитые таблицы, демо. Часть 3: кадры
`PL_PETRI_0..3`, экран и страница «Чашка» на панели.

## Global Constraints

- Сетка 96x96 на торе, U и V int16 Q15 (1.0 = 32767), Δk int8 со шагом 0.012/127.
- Лапласиан 9 точек: стороны 0.2, диагонали 0.05, центр -1; Du = 1, Dv = 0.5, dt = 1.
- Пресеты (F, k): Митоз 0.0367/0.0649 (по умолчанию), Кораллы 0.0545/0.0620, Лабиринт 0.0290/0.0570,
  Черви 0.0780/0.0610, Солитоны 0.0300/0.0620, Коньки 0.0620/0.0609.
- Старт: U = 1, V = 0, 12 капель радиусом 2-5 (U = 0.5, V = 0.25..0.30). Капля по тапу радиус 2.
- Кадр 20 Гц, шагов 0-8 (по умолчанию 4). Δk x0.997 за кадр.
- Максимумы жителей: пожиратели 16, сеятели 4, мутагены 4, вихри 3; старт 3, 1, 2, 1.
- Таблица 256 x int16, модуляции 6 x int8 в порядке pop, dk, vort, energy, jump, dens.
- Без `malloc`, без глобального состояния в компоненте (всё в `petri_t`), свой ГСЧ с seed.
- Комментарии по-русски, код и git по-английски, без длинных тире. Git: `antifreezzz <me@kvatech.ru>`.
- Работа на ветке `petri` от `main`, коммит после каждой задачи.

## Review Focus

- Переполнение Q15 у края диапазона (U около 1 с притоком, большие Δk): значения всегда в [0, 32767].
- Капля и жители на шве тора (x = 0, x = 95.9, отрицательные координаты): пишут в клетки с переносом.
- Все пожиратели вымерли: иммигрант через 100 кадров, если ручка численности пожирателей не 0; при 0 - не приходит.
- Орбита в пустой чашке (V = 0 везде): таблица из нулей, без NaN, модуляция dens = 0.
- Фолд и Край на максимальном контрасте: таблица в пределах int16, без переноса через знак.

---

### Task 1: Сетка Q15 на месте

**Files:**
- Create: `components/petri/include/petri.h`, `components/petri/petri_sim.c`, `components/petri/CMakeLists.txt`
- Modify: `host/CMakeLists.txt` (glob `components/petri/*.c`, include dir в тесты и `acid_render`)
- Test: `host/test_petri_sim.c`

**Interfaces:**
- Produces (в `petri.h`):
  - `#define PETRI_N 96`, `#define PETRI_Q 32767`, `#define PETRI_PRESETS 6`
  - `typedef struct { int16_t u[PETRI_N * PETRI_N], v[PETRI_N * PETRI_N]; int8_t dk[PETRI_N * PETRI_N]; int16_t f_q, k_q; uint32_t rng; /* + поля задач 2-3 */ } petri_t;`
  - `uint32_t petri_rand(uint32_t *s);` - xorshift32, `float petri_randf(uint32_t *s);` - [0, 1)
  - `void petri_init(petri_t *p, uint32_t seed);` - U = 1, V = 0, Δk = 0, пресет 0, `petri_reseed`
  - `void petri_reseed(petri_t *p);` - U = 1, V = 0, 12 капель радиусом 2-5 в случайных местах
  - `void petri_set_fk(petri_t *p, float f, float k);` - в Q15 с округлением
  - `void petri_preset(int i, float *f, float *k);`
  - `void petri_drop(petri_t *p, float cx, float cy, float r);` - U = 0.5, V = 0.25..0.30, перенос через край
  - `void petri_step(petri_t *p);`
  - `float petri_v(const petri_t *p, float x, float y);` - билинейно, перенос, 0..1

- [ ] **Step 1: Write the failing tests** в `host/test_petri_sim.c`:
  - `uniform_stays_uniform`: после `petri_init` обнулить V, U = 32767 везде, 50 `petri_step` - все U == 32767, V == 0.
  - `values_stay_in_range`: init seed 1, `petri_set_fk(0.09, 0.045)`, Δk = 127 везде, 500 шагов - все U и V в [0, 32767].
  - `drop_wraps_torus`: чистая сетка, `petri_drop(p, 0, 0, 2)` - V > 0 в клетках (0,0), (95,0), (0,95), (95,95); `petri_drop(p, -1, 96.5, 1)` - V > 0 в (95, 0).
  - `same_seed_same_dish`: два `petri_init(.., 7)` + по 100 шагов - `memcmp` u и v == 0; seed 8 - отличается.
  - `matches_float_reference`: в тесте свой float-эталон тех же формул с двойным буфером, старт из того же
    состояния (Q15 / 32767). Митоз, 300 шагов: среднее V отличается меньше чем на 10% относительно, доля клеток
    V > 0.1 - меньше чем на 0.05 абсолютно.
  - `presets_stay_alive`: каждый пресет, seed 3, 600 шагов - доля клеток V > 0.1 больше 0.01 и меньше 0.9.
  - `bilinear_read`: V = 0 везде, клетка (10,10) = 32767 - `petri_v(p, 10, 10)` == 1 (±1e-4), `petri_v(p, 10.5, 10)` == 0.5, `petri_v(p, 106, 10)` == 1.

- [ ] **Step 2: Run** `cmake -S host -B host/build && cmake --build host/build` - падает на сборке: нет `petri.h`.

- [ ] **Step 3: Implement** `petri_sim.c`. Шаг на месте, строка за строкой: перед перезаписью строки y хранить
  копии старых строк y-1 и y, плюс копию старой строки 0 для y = 95 (буферы 3 x 96 x 2 в `petri_t`, не на стеке
  задачи панели). Арифметика в int32, сдвиги с округлением (`+ (1 << 14)` перед `>> 15`):
  ```
  s4, sd - суммы соседей по сторонам и диагоналям
  lu  = ((4*s4u + sdu - 20*u) * 1638 + (1<<14)) >> 15          // * 0.05
  lv  = то же для v
  uv  = (u*v + (1<<14)) >> 15;  uvv = (uv*v + (1<<14)) >> 15
  kk  = f_q + k_q + ((dk * 99) >> 5)                            // dk * 0.012/127 в Q15
  u'  = u + lu - uvv + ((f_q * (32767 - u) + (1<<14)) >> 15)
  v'  = v + (lv >> 1) + uvv - ((kk * v + (1<<14)) >> 15)
  ```
  Обе величины зажать в [0, 32767].

- [ ] **Step 4: Run** `cmake --build host/build && ctest --test-dir host/build -R petri` - PASS. Если не проходит
  `matches_float_reference`, искать смещение округления, допуски не расширять.

- [ ] **Step 5: Commit** `feat(petri): gray-scott grid in q15, in place`

### Task 2: Жители

**Files:**
- Create: `components/petri/petri_agents.c`
- Modify: `components/petri/include/petri.h`
- Test: `host/test_petri_agents.c`

**Interfaces:**
- Consumes: задача 1.
- Produces:
  - `enum { PETRI_GRAZER, PETRI_SEEDER, PETRI_MUTAGEN, PETRI_VORTEX, PETRI_KINDS };`
  - `#define PETRI_AGENTS 27` (16 + 4 + 4 + 3)
  - `typedef struct { float x, y, h, energy; uint8_t kind; int8_t sign; uint16_t t; } petri_agent_t;`
  - в `petri_t`: `petri_agent_t a[PETRI_AGENTS]; int n; uint8_t want[PETRI_KINDS]; float appetite, mutation; int immigrant_t; float jump; uint8_t jumps; int8_t flip;`
  - `void petri_set_count(petri_t *p, int kind, int n);` - зажать в максимум, лишних убрать с конца, недостающих родить
  - `int petri_count(const petri_t *p, int kind);`
  - `void petri_frame(petri_t *p, int steps);` - `steps` x `petri_step`, жители, затухание Δk x0.997
  - `petri_init` ставит стартовый набор 3, 1, 2, 1, аппетит 0.15, сила мутагенов 1.

Поведение - как в полной спеке (таблица «Обитатели») с радиусами x0.8: пожиратель лучи дальности 4, ест в радиусе
2.4 (нормировка 1 - r/2.8), скорость 0.5; сеятель капля 2 при приземлении и 1.6 каждые 10 кадров, прыжок с
вероятностью 4.5% за кадр (1.5% при 60 Гц, пересчёт на 20 Гц), длина min(40, 2.4 * u^(-1/1.5)); мутаген радиус 5.6;
вихрь радиус 9, патч 19x19 в `petri_t`. Скорости и шумы курса x3 (кадр 20 Гц вместо 60). Энергия:
`E += 0.135 * eaten - (0.03 + 0.0036 * N)` (x3 к спеке), деление при E > 4.5, гибель при E < 0.
Прыжок: `jump = 1`, `jumps++`; `jump *= 0.68` за кадр (0.88^3). Иммигрант: через 100 кадров без пожирателей,
если `want[PETRI_GRAZER] > 0`. `cosf/sinf` здесь допустимы: это не рендер звука.

- [ ] **Step 1: Write the failing tests:**
  - `start_population`: после init - счётчики 3, 1, 2, 1.
  - `set_count_clamps`: `petri_set_count(p, PETRI_VORTEX, 10)` - 3; `(PETRI_SEEDER, 0)` - 0, остальные не тронуты.
  - `grazers_eat`: V = 0.3 везде, один пожиратель в (48, 48), один кадр с `steps = 0` - V в (48, 48) меньше 0.3, U больше прежнего.
  - `immigrant_returns`: убить всех пожирателей (энергия -1, кадр), 99 кадров - 0 пожирателей, 101-й - 1.
  - `no_immigrant_when_off`: `petri_set_count(p, PETRI_GRAZER, 0)`, 200 кадров - 0.
  - `agents_wrap`: пожиратель в (95.9, 0.1) с курсом в угол, 10 кадров - координаты в [0, 96).
  - `mutagen_marks_dk`: один мутаген, 20 кадров - есть клетки Δk != 0, все в [-127, 127].
  - `seeder_jumps_count`: один сеятель, 1000 кадров - `jumps` от 10 до 100 (ожидание ~45), `jump` в [0, 1].
  - `ecology_survives`: Митоз, seed 5, 2000 кадров по 4 шага - пожирателей больше 0 хотя бы в 90% кадров.

- [ ] **Step 2: Run** - FAIL (нет функций).
- [ ] **Step 3: Implement** `petri_agents.c`.
- [ ] **Step 4: Run** `ctest --test-dir host/build -R petri` - PASS.
- [ ] **Step 5: Commit** `feat(petri): dish inhabitants`

### Task 3: Орбита, рельеф, таблица и модуляции

**Files:**
- Create: `components/petri/petri_capture.c`
- Modify: `components/petri/include/petri.h`
- Test: `host/test_petri_capture.c`

**Interfaces:**
- Consumes: задачи 1-2.
- Produces:
  - `#define PETRI_TABLE 256`, `#define PETRI_MODS 6`
  - `enum { PETRI_HEIGHT, PETRI_EDGE, PETRI_FOLD, PETRI_VOID, PETRI_MODES };`
  - `enum { PETRI_CIRCLE, PETRI_EIGHT, PETRI_LISS32, PETRI_ROSE, PETRI_EPI, PETRI_SHAPES };`
  - в `petri_t`: `uint8_t mode, shape; float contrast /*0..1*/, radius /*2..48*/, drift /*клеток в секунду*/; uint8_t follow; float cx, cy, ang;`
  - `void petri_orbit_point(const petri_t *p, float a, float *x, float *y);`
  - `void petri_move_center(petri_t *p, float dt);` - дрейф по эллипсу радиуса 0.28 N или за первым пожирателем
    (постоянная 0.33 с), перенос через край; `petri_frame` зовёт её с dt = 0.05.
  - `void petri_capture(const petri_t *p, int16_t table[PETRI_TABLE], int8_t mods[PETRI_MODS]);`
  - `petri_init`: режим Высота, Круг, контраст 0, радиус 18, дрейф 2, центр (48, 48).

Формулы рельефа и модуляций - из полной спеки («Рельеф для звука», «Источники модуляции»), с 96 вместо 120 и
расстоянием до вихря / 32 вместо / 40. Шум для Пустоты: хеш координат клетки (`x * 73856093 ^ y * 19349663`,
перемешать, в -1..1), без массива. Билинейное чтение шума - как `petri_v`. После вычитания среднего умножить на
усиление режима (Высота 1, Край 2.5, Фолд 1, Пустота 2.5) и на 16000, зажать в [-32767, 32767].

- [ ] **Step 1: Write the failing tests:**
  - `empty_dish_is_silent`: V = 0 везде, каждый режим кроме Пустоты - таблица из нулей; mods[5] (dens) == 0.
  - `void_mode_sounds_in_empty`: V = 0, Пустота, контраст 0.5 - хотя бы одна точка |t| > 1000.
  - `mean_removed`: Митоз после 600 шагов, Высота - |сумма таблицы| / 256 < 2.
  - `table_in_range_at_max_contrast`: Фолд и Край, контраст 1, 600 шагов - нет значений, ставших отрицательными из-за переполнения (проверить по float-версии формулы в тесте: знак совпадает в каждой точке, где |float| > 0.01).
  - `circle_orbit_points`: центр (48, 48), радиус 10, Круг - точка a = 0 в (58, 48), a = pi/2 в (48, 58).
  - `rose_returns_to_center`: Роза, a = pi/6 - точка в центре (r = R cos 3a = 0).
  - `pop_mod`: 8 пожирателей - mods[0] == 64 (8/16 * 127, ±1).
  - `follow_tracks_grazer`: `follow = 1`, первый пожиратель стоит в (20, 20), 60 вызовов `petri_move_center(p, 0.05)` - центр в пределах 1 клетки от (20, 20).

- [ ] **Step 2: Run** - FAIL.
- [ ] **Step 3: Implement** `petri_capture.c`.
- [ ] **Step 4: Run** `ctest --test-dir host/build` - весь набор PASS (старые тесты не сломаны).
- [ ] **Step 5: Commit** `feat(petri): orbit capture and modulation sources`

### Task 4: Стенд на панели

**Files:**
- Create: `panel/main/petri_bench.c`, `panel/main/petri_bench.h`
- Modify: `panel/CMakeLists.txt` (`EXTRA_COMPONENT_DIRS` + `../components/petri`), `panel/main/CMakeLists.txt`
  (`REQUIRES petri`, файл стенда, define `PETRI_BENCH` при `-DPETRI_BENCH=1` по образцу `main/CMakeLists.txt`),
  `panel/main/main.c` (под `#ifdef PETRI_BENCH` - `petri_bench_start()` после `ui_start()`)

**Interfaces:**
- Consumes: `petri_init`, `petri_frame`, `petri_capture` (задачи 1-3).
- Produces: `void petri_bench_start(void);`

`petri_t` - статический (`.bss`, внутренняя RAM). Задача на ядре 0, приоритет ниже основной петли панели,
кадр 50 мс (`vTaskDelayUntil`). Каждый кадр: `petri_frame(p, 4)` и `petri_capture`. Раз в 5 с лог:
`petri bench: step mean X us, worst Y us; agents Z us; capture W us; frames N; internal RAM free F` и
минимальный свободный стек задачи. Плюс один прогон на старте: 100 шагов подряд, мс на шаг.

- [ ] **Step 1: Сборка** `. ~/esp/esp-idf-v6.1/export.sh && idf.py -C panel -B panel/build-bench -DPETRI_BENCH=1 build` - без ошибок и предупреждений.
- [ ] **Step 2: Прошить** `idf.py -C panel -B panel/build-bench -DPETRI_BENCH=1 -p /dev/ttyUSB0 -b 460800 flash`, лог через pyserial 30 с.
  Ожидание: шаг < 3 мс, internal RAM free > 25 КБ, кадров ~100 за 5 с, `lvgl heap` как раньше, UI и связь
  с синтом живы (крутится карточка, `synth link up`).
- [ ] **Step 3: Записать цифры** в спеку (раздел «Симуляция», строка «Замер 2026-11-..») и в `CLAUDE.md`
  (бюджет панели). Если шаг > 3 мс - потолок шагов на кадр в спеке снизить до значения, при котором 20 кадров/с
  занимают не больше 40% ядра 0.
- [ ] **Step 4: Вернуть обычную прошивку панели** `idf.py -C panel -p /dev/ttyUSB0 -b 460800 flash`.
- [ ] **Step 5: Commit** `test(panel): petri dish bench` (стенд остаётся в коде под флагом).
