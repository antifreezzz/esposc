#pragma once
#include <stdint.h>

#include "acid_custom.h"
#include "acid_midi.h"
#include "acid_mod.h"
#include "acid_synth.h"

// Связка синта с панелью: события BSP и демо идут в синт через фильтр, правки панели - напрямую на страницу.
// Подхват: ручку, поправленную с панели, энкодер BSP не двигает, пока не пересечёт её значение.
// Демо: после первой правки с панели CC демо выбрасываются (ноты, клок и Start идут дальше).

enum { ACID_SRC_BSP, ACID_SRC_DEMO, ACID_SRC_PANEL };

// Что сообщить панели. enc с 0; enc 0 - смена страницы BSP (value - страница).
typedef struct {
    uint8_t page, enc, value, src, bsp_pos, held; // held: 1 - ручка ждёт подхвата BSP
} acid_link_ev_t;

#define ACID_LINK_PEND 8
// Цели макро ставятся не сразу, а шагом связки: не больше стольких ручек за кусок (пересчёт до ~70 мкс на ручку).
#define ACID_LINK_PER_CHUNK 4
#define ACID_LINK_DEFER (ACID_PAGES * ACID_KNOBS)
#define ACID_LINK_PAGES (ACID_PAGE_MOD1 + ACID_MODS)

typedef struct {
    uint8_t bsp_pos[ACID_KNOBS]; // последнее значение энкодера BSP, 0xFF - не крутили
    // Ручка правлена с панели, BSP молчит до пересечения. Свои страницы 14..19, модуляторы 23..26.
    uint8_t held[ACID_LINK_PAGES][ACID_KNOBS];
    int demo_muted;   // панель правила в демо: CC демо больше не идут
    int snapshot_due; // сменился тип синта: панели нужен снимок
    acid_custom_t *custom; // свои страницы, NULL - нет; init не трогает (живут дольше связки)
    acid_mod_t *mod;       // модуляторы, NULL - нет; init не трогает
    int user;              // открытая своя страница 0..5, -1 - штатная s->page
    int mix;               // открытая вкладка MIX инструмента (PL_PAGE_MIX_*, pl_mix.h), -1 - нет
    int mod_page;          // открытая страница модулятора 0..3, -1 - нет
    acid_link_ev_t pend[ACID_LINK_PEND]; // события для панели сверх первого (цели слота)
    int npend, pend_at;
    uint8_t custom_due; // бит своей страницы: панели нужен её кадр определений
    uint8_t mod_due;    // бит модулятора: панели нужен его кадр PL_MOD
    int step_n;         // сколько ручек поставил последний acid_link_step
    // Отложенные цели макро: очередь ручек page * 16 + enc без повторов, последнее значение побеждает.
    uint8_t defer_st[ACID_LINK_DEFER]; // 0 нет в очереди, 1 ждёт, 2 в очереди, но отменена прямой правкой
    uint8_t defer_v[ACID_LINK_DEFER];
    uint8_t defer_q[ACID_LINK_DEFER];
    int dq_head, dq_n;
} acid_link_t;

// И при каждой смене режима демо <-> live. Свои страницы и модуляторы не трогает.
void acid_link_init(acid_link_t *l);
void acid_link_use_custom(acid_link_t *l, acid_custom_t *c);
void acid_link_use_mod(acid_link_t *l, acid_mod_t *m);
// Следующее событие для панели после вызова, который вернул 1 (цели слота своей страницы). 0 - больше нет.
int acid_link_take(acid_link_t *l, acid_link_ev_t *out);
// Страница, которую крутят энкодеры BSP: модулятор, вкладка MIX, своя или штатная.
int acid_link_page(const acid_link_t *l, const acid_synth_t *s);
// Правка цели слота своей страницы (как acid_custom_edit); при успехе - бит в custom_due.
int acid_link_custom_edit(acid_link_t *l, int up, int enc, int idx, int page, int tenc, int lo, int hi);
// Правка цели модулятора: проверяет плавность ручки, ставит бит в mod_due (и при отказе).
int acid_link_mod_edit(acid_link_t *l, const acid_synth_t *s, int k, int idx, int page, int enc);
// Событие BSP или демо (src) в синт через фильтр. Возвращает число событий для панели в out (0 или 1).
int acid_link_midi(acid_link_t *l, acid_synth_t *s, const acid_midi_event_t *ev, int src, acid_link_ev_t *out);
// Правка с панели, enc с 0 (энкодер 1 не трогает), page 0..15. Возвращает 0 или 1 событие в out.
int acid_link_panel_set(acid_link_t *l, acid_synth_t *s, int page, int enc, int v, acid_link_ev_t *out);
// Каждый кусок до рендера: ставит до ACID_LINK_PER_CHUNK отложенных целей макро. Возвращает 0 или 1 событие
// в out, остальные - acid_link_take.
int acid_link_step(acid_link_t *l, acid_synth_t *s, acid_link_ev_t *out);
// На панели открыта страница (штатная, своя или вкладка MIX): энкодеры 2-16 BSP крутят её, пока не тронут энкодер 1. В демо не звать
// (демо само листает страницы). Возвращает 0 или 1 событие (смена страницы) в out.
int acid_link_panel_page(acid_link_t *l, acid_synth_t *s, int page, acid_link_ev_t *out);
