#pragma once
#include <stdint.h>

#include "acid_custom.h"
#include "acid_midi.h"
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

typedef struct {
    uint8_t bsp_pos[ACID_KNOBS]; // последнее значение энкодера BSP, 0xFF - не крутили
    // Ручка правлена с панели, BSP молчит до пересечения. Страницы 10..15 - свои.
    uint8_t held[ACID_PAGES + ACID_USER_PAGES][ACID_KNOBS];
    int demo_muted;   // панель правила в демо: CC демо больше не идут
    int snapshot_due; // сменился тип синта: панели нужен снимок
    acid_custom_t *custom; // свои страницы, NULL - нет; init не трогает (живут дольше связки)
    int user;              // открытая своя страница 0..5, -1 - штатная s->page
    acid_link_ev_t pend[ACID_LINK_PEND]; // события для панели сверх первого (цели слота)
    int npend, pend_at;
    uint8_t custom_due; // бит своей страницы: панели нужен её кадр определений
} acid_link_t;

// И при каждой смене режима демо <-> live. Свои страницы не трогает.
void acid_link_init(acid_link_t *l);
void acid_link_use_custom(acid_link_t *l, acid_custom_t *c);
// Следующее событие для панели после вызова, который вернул 1 (цели слота своей страницы). 0 - больше нет.
int acid_link_take(acid_link_t *l, acid_link_ev_t *out);
// Страница BSP для панели: 10 + своя или штатная.
int acid_link_page(const acid_link_t *l, const acid_synth_t *s);
// Правка цели слота своей страницы (как acid_custom_edit); при успехе - бит в custom_due.
int acid_link_custom_edit(acid_link_t *l, int up, int enc, int idx, int page, int tenc, int lo, int hi);
// Событие BSP или демо (src) в синт через фильтр. Возвращает число событий для панели в out (0 или 1).
int acid_link_midi(acid_link_t *l, acid_synth_t *s, const acid_midi_event_t *ev, int src, acid_link_ev_t *out);
// Правка с панели, enc с 0 (энкодер 1 не трогает), page 0..15. Возвращает 0 или 1 событие в out.
int acid_link_panel_set(acid_link_t *l, acid_synth_t *s, int page, int enc, int v, acid_link_ev_t *out);
// На панели открыта страница: энкодеры 2-16 BSP крутят её, пока не тронут энкодер 1. В демо не звать
// (демо само листает страницы). Возвращает 0 или 1 событие (смена страницы) в out.
int acid_link_panel_page(acid_link_t *l, acid_synth_t *s, int page, acid_link_ev_t *out);
