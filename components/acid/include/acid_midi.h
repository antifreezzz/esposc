#pragma once
#include <stddef.h>
#include <stdint.h>

// Разбор USB-MIDI 1.0: пакеты по 4 байта (CIN, status, d1, d2).
typedef enum {
    ACID_MIDI_NOTE_ON,
    ACID_MIDI_NOTE_OFF,
    ACID_MIDI_CC,
    ACID_MIDI_STOP,
    ACID_MIDI_CLOCK,    // тик, 24 на четверть
    ACID_MIDI_START,
    ACID_MIDI_CONTINUE,
} acid_midi_type_t;

typedef struct {
    uint8_t type; // acid_midi_type_t
    uint8_t ch;   // 0..15
    uint8_t d1;
    uint8_t d2;
} acid_midi_event_t;

// Возвращает число событий в out (не больше max). Нужное нам: ноты, CC, клок, Start, Continue, Stop; остальное пропускается.
int acid_midi_parse(const uint8_t *buf, size_t len, acid_midi_event_t *out, int max);

// CC энкодеров 1..16 BeatStep Pro в Control mode (по умолчанию), индекс = энкодер - 1.
extern const uint8_t acid_bsp_enc_cc[16];
