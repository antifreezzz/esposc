#pragma once
#include <stddef.h>
#include <stdint.h>

// Кадры UART синт <-> панель: 0xA5, тип, длина, полезная нагрузка, CRC-8 (полином 0x07 по типу, длине и нагрузке).
// Битый кадр выбрасывается, приём ищет следующий синхробайт (в том числе внутри выброшенного).
// Длина у каждого типа своя: заголовок с чужой длиной - не кадр, отбрасывается сразу.

#define PL_SYNC 0xA5
#define PL_PAGES 10
#define PL_KNOBS 16
#define PL_PAYLOAD_MAX 255
#define PL_FRAME_MAX (PL_PAYLOAD_MAX + 4)

enum {
    PL_SNAPSHOT = 1, // синт -> панель: всё состояние
    PL_KNOB = 2,     // синт -> панель: одна ручка или смена страницы BSP (enc 0)
    PL_SET = 3,      // панель -> синт: поставить ручку
    PL_HELLO = 4,    // панель -> синт: пришли снимок
};

// Нагрузки - только байты, без выравнивания.
typedef struct {
    uint8_t mode;     // 0 демо, 1 live
    uint8_t bsp_page; // страница BSP
    uint8_t raw[PL_PAGES][PL_KNOBS];
    uint8_t bsp_pos[PL_KNOBS];                // 0xFF - энкодер не крутили
    uint8_t held[(PL_PAGES * PL_KNOBS) / 8]; // бит page * 16 + enc: ручка ждёт подхвата BSP
} pl_snapshot_t;

typedef struct {
    uint8_t page, enc, value, src, bsp_pos, held; // src: 0 BSP, 1 демо, 2 панель
} pl_knob_t;

typedef struct {
    uint8_t page, enc, value;
} pl_set_t;

// Кадр в out (не меньше 4 + len), возвращает его длину; len больше PL_PAYLOAD_MAX - 0.
size_t pl_encode(uint8_t type, const void *payload, size_t len, uint8_t *out);

typedef struct {
    uint8_t buf[PL_FRAME_MAX + 1];
    size_t n;    // байт в buf
    size_t used; // длина выданного кадра: убрать перед следующим разбором
} pl_rx_t;

void pl_rx_init(pl_rx_t *r);
// Байт с линии; 1 - собран кадр (payload живёт до следующего вызова pl_rx_*).
int pl_rx_feed(pl_rx_t *r, uint8_t byte, uint8_t *type, const uint8_t **payload, size_t *len);
// Следующий уже собранный кадр без нового байта (после ложного синхробайта их может быть несколько).
int pl_rx_next(pl_rx_t *r, uint8_t *type, const uint8_t **payload, size_t *len);
