#include "acid_custom.h"

#include <string.h>

#define TYPE_ENC 1
#define PAGE_SYNTH1 6

static const uint8_t MAGIC[4] = {'C', 'U', 'S', 1};

void acid_custom_init(acid_custom_t *c)
{
    memset(c->t, ACID_CUSTOM_NONE, sizeof c->t);
    memset(c->val, 0, sizeof c->val);
    c->save = 0;
}

static int slot_ok(int up, int enc)
{
    return up >= 0 && up < ACID_USER_PAGES && enc > 0 && enc < ACID_KNOBS;
}

static int target_ok(int page, int enc, int lo, int hi)
{
    if (page < 0 || page >= ACID_PAGES || enc <= 0 || enc >= ACID_KNOBS) {
        return 0;
    }
    if (enc == TYPE_ENC && (page == PAGE_SYNTH1 || page == PAGE_SYNTH1 + 1 || page == ACID_PAGE_LAYER1 ||
                             page == ACID_PAGE_LAYER1 + 1)) {
        return 0; // смена типа меняет смысл всей страницы
    }
    return lo >= 0 && lo <= 127 && hi >= 0 && hi <= 127;
}

int acid_custom_count(const acid_custom_t *c, int up, int enc)
{
    if (!slot_ok(up, enc)) {
        return 0;
    }
    int n = 0;
    while (n < ACID_USER_TARGETS && c->t[up][enc][n].page != ACID_CUSTOM_NONE) {
        n++;
    }
    return n;
}

int acid_custom_edit(acid_custom_t *c, int up, int enc, int idx, int page, int tenc, int lo, int hi)
{
    if (!slot_ok(up, enc) || idx < 0 || idx >= ACID_USER_TARGETS) {
        return 0;
    }
    acid_target_t *t = c->t[up][enc];
    int n = acid_custom_count(c, up, enc);
    if (page == ACID_CUSTOM_NONE) {
        if (idx >= n) {
            return 0;
        }
        memmove(&t[idx], &t[idx + 1], (size_t)(ACID_USER_TARGETS - 1 - idx) * sizeof *t);
        memset(&t[ACID_USER_TARGETS - 1], ACID_CUSTOM_NONE, sizeof *t);
    } else {
        if (idx > n || !target_ok(page, tenc, lo, hi)) {
            return 0;
        }
        t[idx] = (acid_target_t){(uint8_t)page, (uint8_t)tenc, (uint8_t)lo, (uint8_t)hi};
    }
    c->save = 1;
    return 1;
}

int acid_custom_is_alias(const acid_custom_t *c, int up, int enc)
{
    if (acid_custom_count(c, up, enc) != 1) {
        return 0;
    }
    const acid_target_t *t = &c->t[up][enc][0];
    return t->lo == 0 && t->hi == 127;
}

int acid_custom_map(const acid_target_t *t, int v)
{
    int d = t->hi - t->lo;
    return t->lo + (d * v + (d >= 0 ? 63 : -63)) / 127;
}

int acid_custom_value(const acid_custom_t *c, const acid_synth_t *s, int up, int enc)
{
    if (!slot_ok(up, enc)) {
        return 0;
    }
    if (acid_custom_is_alias(c, up, enc)) {
        const acid_target_t *t = &c->t[up][enc][0];
        return acid_synth_knob(s, t->page, t->enc);
    }
    return c->val[up][enc];
}

size_t acid_custom_export(const acid_custom_t *c, void *buf, size_t cap)
{
    if (cap < ACID_CUSTOM_BLOB) {
        return 0;
    }
    uint8_t *b = buf;
    memcpy(b, MAGIC, sizeof MAGIC);
    memcpy(b + sizeof MAGIC, c->t, sizeof c->t);
    memcpy(b + sizeof MAGIC + sizeof c->t, c->val, sizeof c->val);
    return ACID_CUSTOM_BLOB;
}

int acid_custom_import(acid_custom_t *c, const void *buf, size_t len)
{
    const uint8_t *b = buf;
    if (len != ACID_CUSTOM_BLOB || memcmp(b, MAGIC, sizeof MAGIC) != 0) {
        return 0;
    }
    static acid_target_t t[ACID_USER_PAGES][ACID_KNOBS][ACID_USER_TARGETS];
    memcpy(t, b + sizeof MAGIC, sizeof t);
    for (int u = 0; u < ACID_USER_PAGES; u++) {
        for (int e = 0; e < ACID_KNOBS; e++) {
            for (int i = 0; i < ACID_USER_TARGETS; i++) {
                const acid_target_t *x = &t[u][e][i];
                if (x->page != ACID_CUSTOM_NONE && (e == 0 || !target_ok(x->page, x->enc, x->lo, x->hi))) {
                    return 0; // битая цель: не применяем наполовину
                }
            }
        }
    }
    memcpy(c->t, t, sizeof t);
    memcpy(c->val, b + sizeof MAGIC + sizeof t, sizeof c->val);
    for (int u = 0; u < ACID_USER_PAGES; u++) {
        for (int e = 0; e < ACID_KNOBS; e++) {
            c->val[u][e] = c->val[u][e] > 127 ? 127 : c->val[u][e];
        }
    }
    return 1;
}
