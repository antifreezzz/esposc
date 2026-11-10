#include "pl_mod.h"

#include "pl_labels.h"
#include "pl_pick.h"

// Страницы синта, слои и Grids: то же, что acid_mod_page_ok.
static int page_ok(int page, int enc)
{
    return enc >= 1 && enc <= 15 && ((page >= 0 && page <= 8) || (page >= 10 && page <= 12));
}

int pl_mod_can_target(const pl_model_t *m, int page, int enc)
{
    int tp, te;
    return page_ok(page, enc) && pl_knob_label(page, enc, m->raw[page]) && pl_pick_view(m, page, enc, &tp, &te) == PL_PICK_NONE;
}

int pl_mod_page_has_target(const pl_model_t *m, int page)
{
    for (int e = 1; e < PL_KNOBS; e++) {
        if (pl_mod_can_target(m, page, e)) {
            return 1;
        }
    }
    return 0;
}

int pl_mod_offset(int out, int amount, int depth)
{
    float o = (float)out / 127.0f;
    float x = o * (float)amount * (2.0f / 127.0f) * (float)(depth - 64);
    return (int)(x + (x < 0.0f ? -0.5f : 0.5f));
}

int pl_mod_sum(const pl_model_t *m, int page, int enc, int *n)
{
    int sum = 0, cnt = 0;
    if (page_ok(page, enc) && pl_mod_can_target(m, page, enc)) {
        for (int k = 0; k < PL_MOD_PAGES; k++) {
            for (int i = 0; i < PL_MOD_TARGETS; i++) {
                if (m->mod_t[k][i].page == page && m->mod_t[k][i].enc == enc) {
                    sum += pl_mod_offset(m->mod_out[k], m->mod_knob[k][8], m->mod_knob[k][9 + i]);
                    cnt++;
                }
            }
        }
    }
    if (n) {
        *n = cnt;
    }
    return sum;
}

int pl_mod_effective(const pl_model_t *m, int page, int enc)
{
    if (page < 0 || page >= PL_PAGES || enc < 0 || enc >= PL_KNOBS) {
        return 0;
    }
    int v = m->raw[page][enc] + pl_mod_sum(m, page, enc, NULL);
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

int pl_mod_count(const pl_model_t *m, int k)
{
    int n = 0;
    for (int i = 0; k >= 0 && k < PL_MOD_PAGES && i < PL_MOD_TARGETS; i++) {
        n += m->mod_t[k][i].page != PL_NONE;
    }
    return n;
}

int pl_mod_edit(pl_model_t *m, int k, int idx, int page, int enc, pl_mod_edit_t *out)
{
    if (k < 0 || k >= PL_MOD_PAGES || idx < 0 || idx >= PL_MOD_TARGETS || !(m->mod_have >> k & 1)) {
        return 0;
    }
    if (page == PL_NONE) {
        enc = 0;
    } else if (!pl_mod_can_target(m, page, enc)) {
        return 0;
    }
    m->mod_t[k][idx] = (pl_mod_target_t){(uint8_t)page, (uint8_t)enc};
    *out = (pl_mod_edit_t){(uint8_t)k, (uint8_t)idx, (uint8_t)page, (uint8_t)enc};
    return 1;
}
