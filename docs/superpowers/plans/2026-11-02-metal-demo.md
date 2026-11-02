# Метал-демо и выбор демо на панели: план

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** новое демо `acid_metal_song` (150 BPM) по умолчанию, акид остаётся, тап по статусу панели переключает песню.

**Architecture:** темп - поле песни, плеер считает шаг и клок от него. Песня выбирается в `main.c` из таблицы;
панель шлёт кадр `PL_DEMO`, синт перезапускает демо с нуля следующей песней, снимок несёт номер песни.

**Tech Stack:** C11, ESP-IDF 6.1, LVGL на панели, хост-тесты `host/test.h`.

**Spec:** `docs/superpowers/specs/2026-11-02-metal-demo-design.md`

## Global Constraints

- Акид бит в бит как в `main` (`acid_render` без аргумента песни - акид).
- Метал: 25 кусков на тик (150 BPM), акид: 30.
- TDD, коммит на задачу, git-личность `antifreezzz <me@kvatech.ru>`, комментарии по-русски, без длинных тире.
- В рендере никаких локов, аллокаций, логов.

## Review Focus

- Тап по статусу в live или без связи - ничего не шлёт (тест в задаче 4).
- Песня с `tick == 0` (старые тестовые песни) играет как 30 (тест в задаче 1).
- Смена песни посреди части: ни одна нота не висит (синт с нуля, `enter_mode`).
- Старая панель с новым синтом: снимок другой длины отбрасывается, панель ждёт - прошивать обе.
- Стыки частей метала: пачка ручек в один кусок не дороже, чем у акида (замер на плате, задача 6).

---

### Task 1: темп на песню

**Files:** Modify `components/acid/include/acid_demo.h`, `components/acid/acid_demo.c`; Test `host/test_demo.c`

**Interfaces:** Produces `acid_demo_song_t.tick` (int, кусков на тик; 0 - `ACID_DEMO_TICK`), шаг = 6 * tick.

- [ ] Тест `test_demo_tick_per_song`: песня c `.tick = 25` - клок каждые 25 кусков, нота синта 1 шага 1 ровно на куске 150, длина круга 1 такт = 16 * 150.
- [ ] Красный, затем плеер: `step` и `tick` в `acid_demo_t` из песни при init.
- [ ] Все хост-тесты зелёные, `acid_render` акида бит в бит с `main`. Коммит.

### Task 2: песня метала и смоук на обеих

**Files:** Create `components/acid/acid_metal_song.c`; Modify `components/acid/CMakeLists.txt`, `acid_demo.h`
(`extern const acid_demo_song_t acid_metal_song;`), `host/test_demo_smoke.c`, `host/render_wav.c`, `host/test_synth.c` по нужде.

- [ ] Смоук: `play_cycle` и проверки охвата - функция от песни, гоняется на `acid_demo_song` и `acid_metal_song`;
      проверка числа ручек частей - тоже на обеих. Красный (песни нет).
- [ ] Песня: 150 BPM, форма из спеки, свой материал. Слушать через `acid_render` (аргумент песни: `acid`/`metal`).
- [ ] Зелёный смоук, пик WAV ниже 0 dBFS. Коммит.

### Task 3: кадр выбора демо и номер песни в снимке

**Files:** Modify `components/panel_link/include/pl_proto.h`, `pl_proto.c`, `pl_model.h/.c`; Test `host/test_proto.c`, `host/test_model.c`

**Interfaces:** `PL_DEMO = 11` (панель -> синт, без нагрузки, «следующая песня»); `pl_snapshot_t.demo` (номер песни,
последний байт); `pl_model_t.demo`. Номера: 0 METAL, 1 ACID.

- [ ] Тесты: кадр `PL_DEMO` длины 0 проходит разбор; снимок с `demo = 1` даёт `m->demo == 1`. Красный, код, зелёный. Коммит.

### Task 4: панель - надпись и тап

**Files:** Modify `components/panel_link/include/pl_view.h`, `pl_view.c`, `panel/main/ui.c`, `panel/main/link.c/.h`; Test `host/test_view.c`

**Interfaces:** `const char *pl_view_status(const pl_model_t *m, int linked)` - "NO LINK", "LIVE", "METAL", "ACID";
`int pl_view_demo_tap(const pl_model_t *m, int linked, int x0, int y0, int x1, int y1)` - 1, если тап в шапке
правее `EDIT_X1 + 20` (432) в демо со связью; `void link_demo(void)`.

- [ ] Тесты на обе функции (live и без связи - 0 и прежние тексты). Красный, код, зелёный.
- [ ] `ui.c`: `draw_status` через `pl_view_status`, в `input` до прочей шапки - `pl_view_demo_tap` -> `link_demo()`.
- [ ] `idf.py -C panel build` зелёный. Коммит.

### Task 5: синт - таблица песен и переключение

**Files:** Modify `main/main.c`, `main/panel_uart.c/.h`

- [ ] `static const acid_demo_song_t *const DEMO_SONGS[] = {&acid_metal_song, &acid_demo_song}`, `s_demo_song`.
      Стенд `ESPOSC_STRESS` как был (одна песня).
- [ ] `panel_uart_receive_demo()` (флаг из задачи связи); в цикле синта в демо - уход громкости, `s_demo_song`
      следующий, `enter_mode(0, ...)`, подъём громкости. Снимок: `demo = s_demo_song`.
- [ ] `idf.py build` зелёный. Коммит.

### Task 6: железо и доки

- [ ] Прошить синт и панель, лог: худшее по ядрам на метале не выше, чем на акиде; ступень защиты 0.
- [ ] `README.md` (демо, выбор), `docs/roadmap.md`, `CLAUDE.md` (bit-exact - про акид). Коммит.
