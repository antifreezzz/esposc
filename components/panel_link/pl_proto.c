#include "pl_proto.h"

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
