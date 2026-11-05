#include "acid_scene.h"

#include <string.h>

#include "pl_labels.h"

#define TYPE_ENC 1
#define PAGE_SYNTH1 6

static const uint8_t MAGIC[4] = {'S', 'C', 'N', 4}; // 3: 13 страниц (Grids), 4: 14 (чашка Петри)
#define OLD_BLOB(pages) (4 + 1 + ACID_SCENES * (pages) * ACID_KNOBS + sizeof(((acid_scene_t *)0)->user))
// Старые версии: 1 - 10 страниц (без слоёв), 2 - 12 (без Grids).
static const struct {
    uint8_t ver, pages;
} OLD[] = {{1, 10}, {2, 12}, {3, 13}};
#define UNSET 0xFF // ручка в сцене не задана (старая сцена без слоёв или Grids): вызов её не трогает

void acid_scene_init(acid_scene_t *sc)
{
    memset(sc, 0, sizeof *sc);
    sc->cur = -1;
    sc->idle_chunks = ACID_SCENE_NOCLOCK; // клока ещё не было
}

void acid_scene_store(acid_scene_t *sc, int n, const acid_synth_t *s, const acid_custom_t *c)
{
    if (n < 0 || n >= ACID_SCENES) {
        return;
    }
    for (int p = 0; p < ACID_PAGES; p++) {
        sc->knob[n][p][0] = 0;
        for (int e = 1; e < ACID_KNOBS; e++) {
            sc->knob[n][p][e] = (uint8_t)acid_synth_knob(s, p, e);
        }
    }
    // Вкл и «сбивка сейчас» у Grids - действия, не звук: вызов сцены их не трогает.
    sc->knob[n][ACID_PAGE_GRIDS][DRM_ON] = UNSET;
    sc->knob[n][ACID_PAGE_GRIDS][DRM_FILL_NOW] = UNSET;
    if (c) {
        memcpy(sc->user[n], c->val, sizeof sc->user[n]);
    } else {
        memset(sc->user[n], 0, sizeof sc->user[n]);
    }
    sc->used |= (uint8_t)(1 << n);
    sc->save = 1;
}

int acid_scene_recall(acid_scene_t *sc, int n, int bars)
{
    if (n < 0 || n >= ACID_SCENES || !(sc->used & (1 << n))) {
        return 0;
    }
    sc->cur = n;
    sc->bars = bars < 0 ? 0 : bars > 8 ? 8 : bars;
    sc->state = 1; // идущий переход замирает на текущих значениях, новый начнётся от них
    return 1;
}

// Страница с типом синтеза: синты и их слои.
static int typed(int p)
{
    return p == PAGE_SYNTH1 || p == PAGE_SYNTH1 + 1 || p == ACID_PAGE_LAYER1 || p == ACID_PAGE_LAYER1 + 1;
}

static int is_type(int p, int e)
{
    return e == TYPE_ENC && typed(p);
}

// Список (модель, время дилэя, транспозиция...) по значениям сцены: такие ручки прыгают.
static int is_list(const uint8_t page_raw[ACID_KNOBS], int p, int e)
{
    int lo, hi;
    pl_knob_range(p, e, page_raw, &lo, &hi);
    return lo != 0 || hi != 127;
}

// Черта такта: переход начинается со снятия значений «откуда» и какие ручки прыгают (списки; у синта
// со сменой типа - вся страница, там память другого типа). Снятие - по ACID_SCENE_SCAN_PER_CHUNK позиций
// за кусок: все 150 разом стоили ~160 мкс одного куска.
static void begin(acid_scene_t *sc)
{
    sc->prep = 0;
    sc->ticks = 0;
    sc->cursor = 0;
    sc->since_snap = 0;
    sc->final_sweeps = 0;
    sc->dirty = 1;
    sc->state = 2;
}

// 1 - снятие закончено (тогда же встают значения слотов своих страниц).
static int prepare(acid_scene_t *sc, acid_link_t *l, const acid_synth_t *s)
{
    const int n = ACID_PAGES * (ACID_KNOBS - 1);
    for (int i = 0; i < ACID_SCENE_SCAN_PER_CHUNK && sc->prep < n; i++, sc->prep++) {
        int p = sc->prep / (ACID_KNOBS - 1), e = 1 + sc->prep % (ACID_KNOBS - 1);
        const uint8_t *to = sc->knob[sc->cur][p];
        int retype = typed(p) && to[TYPE_ENC] != UNSET &&
                     acid_synth_knob(s, p, TYPE_ENC) != to[TYPE_ENC];
        sc->from[p][e] = (uint8_t)acid_synth_knob(s, p, e);
        sc->jump[p][e] = (uint8_t)(retype || is_list(to, p, e));
    }
    if (sc->prep < n) {
        return 0;
    }
    acid_custom_t *c = l->custom;
    if (c) {
        for (int u = 0; u < ACID_USER_PAGES; u++) {
            for (int e = 1; e < ACID_KNOBS; e++) {
                if (!acid_custom_is_alias(c, u, e)) {
                    c->val[u][e] = sc->user[sc->cur][u][e]; // цели и так из сцены: макро их не двигает
                }
                if (acid_custom_count(c, u, e)) {
                    l->held[ACID_USER_FIRST + u][e] = 1; // BSP стоит иначе: подхват, как у штатных ручек
                }
            }
        }
        c->save = 1;
        l->custom_due = (1 << ACID_USER_PAGES) - 1;
    }
    return 1;
}

static int want(const acid_scene_t *sc, int p, int e)
{
    const uint8_t *to = sc->knob[sc->cur][p];
    int span = sc->bars * ACID_SCENE_BAR;
    if (span == 0 || sc->jump[p][e]) {
        return to[e];
    }
    int t = sc->ticks < span ? sc->ticks : span;
    int d = to[e] - sc->from[p][e];
    return sc->from[p][e] + (d * t + (d >= 0 ? span / 2 : -span / 2)) / span;
}

static void put(acid_link_t *l, acid_synth_t *s, int p, int e, int v)
{
    acid_link_ev_t ev;
    acid_link_panel_set(l, s, p, e, v, &ev); // как правка с панели: подхват BSP, демо молчит
}

int acid_scene_step(acid_scene_t *sc, acid_link_t *l, acid_synth_t *s)
{
    int ph = s->tonal.phase;
    int d = (ph - sc->last_phase + ACID_SCENE_BAR) % ACID_SCENE_BAR;
    int bar = ph < sc->last_phase; // оборот фазы: черта такта (и Start)
    sc->last_phase = ph;
    if (d == 0 && !bar) {
        sc->idle_chunks += sc->idle_chunks < ACID_SCENE_NOCLOCK;
    } else {
        sc->idle_chunks = 0;
    }
    int noclock = sc->idle_chunks >= ACID_SCENE_NOCLOCK;
    if (sc->state == 0) {
        return 0;
    }
    if (sc->state == 1) {
        if (!noclock && !bar) {
            return 0;
        }
        if (noclock) {
            sc->bars = 0;
        }
        begin(sc);
    } else {
        sc->ticks += d;
        if (noclock) {
            sc->bars = 0; // клок пропал посреди перехода: доходим сразу
        }
    }
    if (sc->prep < ACID_PAGES * (ACID_KNOBS - 1)) {
        prepare(sc, l, s); // в этом куске ручки не ставим: снятие и так полный кусок работы
        return 0;
    }
    int budget = ACID_SCENE_PER_CHUNK;
    static const int TYPED[4] = {PAGE_SYNTH1, PAGE_SYNTH1 + 1, ACID_PAGE_LAYER1, ACID_PAGE_LAYER1 + 1};
    for (int k = 0; k < 4 && budget > 0; k++) {
        int p = TYPED[k], to = sc->knob[sc->cur][p][TYPE_ENC];
        if (to != UNSET && acid_synth_knob(s, p, TYPE_ENC) != to) {
            put(l, s, p, TYPE_ENC, to); // тип первым: ручки дальше пишутся в память нового типа
            budget--;
        }
    }
    const int n = ACID_PAGES * (ACID_KNOBS - 1);
    int done = sc->bars == 0 || sc->ticks >= sc->bars * ACID_SCENE_BAR;
    for (int i = 0; i < ACID_SCENE_SCAN_PER_CHUNK && budget > 0; i++) {
        int p = sc->cursor / (ACID_KNOBS - 1), e = 1 + sc->cursor % (ACID_KNOBS - 1);
        if (!is_type(p, e) && sc->knob[sc->cur][p][e] != UNSET) {
            int v = want(sc, p, e);
            if (acid_synth_knob(s, p, e) != v) {
                put(l, s, p, e, v);
                budget--;
                sc->dirty = 1;
            }
        }
        if (++sc->cursor == n) {
            sc->cursor = 0;
            // Полный проход после конца перехода без единой правки - всё на месте. Ручку, которую синт
            // не принимает, не ждём вечно: третий такой проход заканчивает переход.
            int clean = !sc->dirty;
            sc->dirty = 0;
            if (done && (clean || ++sc->final_sweeps >= 3)) {
                sc->state = 0;
                return 1;
            }
        }
    }
    if (++sc->since_snap >= ACID_SCENE_SNAP_EVERY) {
        sc->since_snap = 0;
        return 1;
    }
    return 0;
}

size_t acid_scene_export(const acid_scene_t *sc, void *buf, size_t cap)
{
    if (cap < ACID_SCENE_BLOB) {
        return 0;
    }
    uint8_t *b = buf;
    memcpy(b, MAGIC, sizeof MAGIC);
    b[4] = sc->used;
    memcpy(b + 5, sc->knob, sizeof sc->knob);
    memcpy(b + 5 + sizeof sc->knob, sc->user, sizeof sc->user);
    return ACID_SCENE_BLOB;
}

// Старый blob (pages страниц): эти страницы как были, остальные не заданы.
static int import_old(acid_scene_t *sc, const uint8_t *b, int pages, int v1)
{
    size_t len = OLD_BLOB(pages);
    for (size_t i = 5; i < len; i++) {
        if (b[i] > 127 && (v1 || i >= 5 + (size_t)ACID_SCENES * pages * ACID_KNOBS || b[i] != UNSET)) {
            return 0;
        }
    }
    sc->used = b[4];
    memset(sc->knob, UNSET, sizeof sc->knob);
    for (int n = 0; n < ACID_SCENES; n++) {
        memcpy(sc->knob[n], b + 5 + n * pages * ACID_KNOBS, (size_t)pages * ACID_KNOBS);
    }
    memcpy(sc->user, b + 5 + ACID_SCENES * pages * ACID_KNOBS, sizeof sc->user);
    for (int n = 0; n < ACID_SCENES; n++) {
        for (int k = 0; k < ACID_SLOTS; k++) {
            uint8_t *t = &sc->knob[n][ACID_PAGE_LAYER1 + k][1];
            if (*t == 25) {
                *t = ACID_LAYER_OFF; // до PETRI 25 был «выкл» слоя, теперь это PETRI, которого у слоя нет
            }
        }
    }
    return 1;
}

int acid_scene_import(acid_scene_t *sc, const void *buf, size_t len)
{
    const uint8_t *b = buf;
    for (size_t k = 0; k < sizeof OLD / sizeof OLD[0]; k++) {
        if (len == OLD_BLOB(OLD[k].pages) && memcmp(b, MAGIC, 3) == 0 && b[3] == OLD[k].ver) {
            return import_old(sc, b, OLD[k].pages, OLD[k].ver == 1);
        }
    }
    if (len != ACID_SCENE_BLOB || memcmp(b, MAGIC, sizeof MAGIC) != 0) {
        return 0;
    }
    for (size_t i = 5; i < len; i++) {
        if (b[i] > 127 && !(i < 5 + sizeof sc->knob && b[i] == UNSET)) {
            return 0;
        }
    }
    sc->used = b[4];
    memcpy(sc->knob, b + 5, sizeof sc->knob);
    memcpy(sc->user, b + 5 + sizeof sc->knob, sizeof sc->user);
    return 1;
}
