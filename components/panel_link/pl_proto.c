#include "pl_proto.h"

#include "pl_load.h"

#include <string.h>

static uint8_t crc8(uint8_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) {
            c = (uint8_t)(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
        }
    }
    return c;
}

size_t pl_encode(uint8_t type, const void *payload, size_t len, uint8_t *out)
{
    if (len > PL_PAYLOAD_MAX) {
        return 0;
    }
    out[0] = PL_SYNC;
    out[1] = type;
    out[2] = (uint8_t)len;
    if (len) {
        memcpy(out + 3, payload, len);
    }
    out[3 + len] = crc8(0, out + 1, len + 2);
    return len + 4;
}

// Длина нагрузки по типу; -1 - не наш тип. Ложный синхробайт в мусоре отбрасывается на заголовке,
// иначе приёмник ждал бы до 255 байт «кадра» и глотал настоящие (мусор на линии, пока соседа нет).
static int payload_len(uint8_t type)
{
    switch (type) {
    case PL_SNAPSHOT: return (int)sizeof(pl_snapshot_t);
    case PL_KNOB: return (int)sizeof(pl_knob_t);
    case PL_SET: return (int)sizeof(pl_set_t);
    case PL_HELLO: return 0;
    case PL_PAGE: return 1;
    case PL_CUSTOM: return (int)sizeof(pl_custom_t);
    case PL_CUSTOM_EDIT: return (int)sizeof(pl_custom_edit_t);
    case PL_SCENE: return (int)sizeof(pl_scene_t);
    case PL_SCENE_STATE: return (int)sizeof(pl_scene_state_t);
    case PL_LOAD: return (int)sizeof(pl_load_t);
    case PL_DEMO: return 0;
    case PL_GRIDS_STEP: return 1;
    case PL_SNAPSHOT2: return (int)sizeof(pl_snapshot2_t);
    case PL_PETRI_0:
    case PL_PETRI_1:
    case PL_PETRI_2: return PL_PETRI_PART;
    case PL_PETRI_3: return PL_PETRI_PART_MAX;
    case PL_SEQ_STEP: return 4;
    case PL_SEQ_OCT: return 3;
    case PL_SEQ_ROW: return 3 + PL_SEQ_CELLS;
    case PL_SEQ_CTRL: return 3;
    case PL_SEQ_BPM: return 1;
    case PL_SEQ_DUMP: return (int)sizeof(pl_seq_dump_t);
    case PL_SEQ_POS: return (int)sizeof(pl_seq_pos_t);
    case PL_SEQ_SCALE: return 3;
    case PL_MODE: return 1;
    default: return -1;
    }
}

void pl_rx_init(pl_rx_t *r)
{
    r->n = 0;
    r->used = 0;
}

static void drop(pl_rx_t *r, size_t k)
{
    memmove(r->buf, r->buf + k, r->n - k);
    r->n -= k;
}

int pl_rx_next(pl_rx_t *r, uint8_t *type, const uint8_t **payload, size_t *len)
{
    if (r->used) {
        drop(r, r->used);
        r->used = 0;
    }
    for (;;) {
        size_t s = 0;
        while (s < r->n && r->buf[s] != PL_SYNC) {
            s++;
        }
        drop(r, s);
        if (r->n >= 2 && payload_len(r->buf[1]) < 0) {
            drop(r, 1);
            continue;
        }
        if (r->n >= 3 && payload_len(r->buf[1]) != r->buf[2]) {
            drop(r, 1);
            continue;
        }
        if (r->n < 3 || r->n < (size_t)r->buf[2] + 4) {
            return 0; // кадр ещё не пришёл целиком
        }
        size_t n = r->buf[2];
        if (crc8(0, r->buf + 1, n + 2) == r->buf[3 + n]) {
            *type = r->buf[1];
            *payload = r->buf + 3;
            *len = n;
            r->used = n + 4;
            return 1;
        }
        drop(r, 1); // ложный синхробайт: ищем следующий внутри
    }
}

int pl_rx_feed(pl_rx_t *r, uint8_t byte, uint8_t *type, const uint8_t **payload, size_t *len)
{
    if (r->used) {
        drop(r, r->used);
        r->used = 0;
    }
    r->buf[r->n++] = byte; // буфер не переполнится: кадр решается не позже PL_FRAME_MAX байт
    return pl_rx_next(r, type, payload, len);
}

int pl_page_bsp(int page)
{
    if (page >= PL_PAGE_LAYER1 && page < PL_PAGE_LAYER1 + 2) {
        return 16 + page - PL_PAGE_LAYER1; // слои
    }
    if (page == PL_PAGE_GRIDS) {
        return -1;
    }
    if (page == PL_PAGE_PETRI) {
        return 18;
    }
    if (page >= PL_USER_FIRST) {
        return 10 + page - PL_USER_FIRST;
    }
    return page;
}

void pl_petri_split(const int16_t tab[PL_PETRI_POINTS], const int8_t mods[6], uint8_t jumps, uint8_t seq,
                    uint8_t out[4][PL_PETRI_PART_MAX], size_t len[4])
{
    for (int k = 0; k < 4; k++) {
        uint8_t *o = out[k];
        o[0] = seq;
        for (int i = 0; i < 64; i++) {
            uint16_t v = (uint16_t)tab[k * 64 + i];
            o[1 + 2 * i] = (uint8_t)(v & 0xFF);
            o[2 + 2 * i] = (uint8_t)(v >> 8);
        }
        len[k] = PL_PETRI_PART;
    }
    memcpy(out[3] + PL_PETRI_PART, mods, 6);
    out[3][PL_PETRI_PART + 6] = jumps;
    out[3][PL_PETRI_PART + 7] = 0; // флаги: пока нет
    len[3] = PL_PETRI_PART_MAX;
}

int pl_petri_collect(pl_petri_rx_t *r, uint8_t type, const uint8_t *payload, size_t len)
{
    if (type < PL_PETRI_0 || type > PL_PETRI_3 || (int)len != payload_len(type)) {
        return 0;
    }
    int k = type - PL_PETRI_0;
    if (payload[0] != r->seq) {
        r->seq = payload[0];
        r->have = 0;
    }
    for (int i = 0; i < 64; i++) {
        r->tab[k * 64 + i] = (int16_t)(payload[1 + 2 * i] | payload[2 + 2 * i] << 8);
    }
    if (k == 3) {
        memcpy(r->mods, payload + PL_PETRI_PART, 6);
        r->jumps = payload[PL_PETRI_PART + 6];
        r->flags = payload[PL_PETRI_PART + 7];
    }
    r->have |= (uint8_t)(1 << k);
    if (r->have == 0xF) {
        r->have = 0; // повтор тех же частей не даст второй таблицы
        return 1;
    }
    return 0;
}

int pl_petri_synth(const uint8_t raw[PL_PAGES][PL_KNOBS])
{
    return raw[PL_PAGE_SYNTH1][1] == PL_TYPE_PETRI ? 0 : raw[PL_PAGE_SYNTH1 + 1][1] == PL_TYPE_PETRI ? 1 : -1;
}

int pl_mode_ok(int live, int mode)
{
    if (live) {
        return mode == PL_MODE_LIVE || mode == PL_DEMO_DRONE;
    }
    return mode >= PL_DEMO_METAL && mode <= PL_DEMO_SEQ;
}
