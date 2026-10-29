#include "acid_midi.h"

int acid_midi_parse(const uint8_t *buf, size_t len, acid_midi_event_t *out, int max)
{
    int n = 0;
    for (size_t i = 0; i + 4 <= len && n < max; i += 4) {
        const uint8_t *p = buf + i;
        uint8_t cin = p[0] & 0x0F; // старший полубайт - номер кабеля, нам не важен
        // Байты данных 7-битные: маска не пускает мусор битого пакета в параметры.
        acid_midi_event_t e = {.ch = p[1] & 0x0F, .d1 = p[2] & 0x7F, .d2 = p[3] & 0x7F};
        if (cin == 0x9) {
            e.type = e.d2 ? ACID_MIDI_NOTE_ON : ACID_MIDI_NOTE_OFF;
        } else if (cin == 0x8) {
            e.type = ACID_MIDI_NOTE_OFF;
        } else if (cin == 0xB) {
            e.type = ACID_MIDI_CC;
        } else if (cin == 0xF && (p[1] == 0xFC || p[1] == 0xF8 || p[1] == 0xFA || p[1] == 0xFB)) {
            e.type = p[1] == 0xFC ? ACID_MIDI_STOP
                   : p[1] == 0xF8 ? ACID_MIDI_CLOCK
                   : p[1] == 0xFA ? ACID_MIDI_START : ACID_MIDI_CONTINUE;
            e.ch = 0;
        } else {
            continue;
        }
        out[n++] = e;
    }
    return n;
}
