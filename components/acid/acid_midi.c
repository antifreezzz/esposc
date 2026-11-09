#include "acid_midi.h"

const uint8_t acid_bsp_enc_cc[16] = {10, 74, 71, 76, 77, 93, 73, 75, 114, 18, 19, 16, 17, 91, 79, 72};

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

void acid_midi_count(acid_midi_stats_t *s, const acid_midi_event_t *ev, int n)
{
    for (int i = 0; i < n; i++) {
        switch (ev[i].type) {
        case ACID_MIDI_NOTE_ON:
        case ACID_MIDI_NOTE_OFF: s->notes[ev[i].ch & 15]++; break;
        case ACID_MIDI_CC: s->cc[ev[i].ch & 15]++; break;
        case ACID_MIDI_CLOCK: s->clock++; break;
        case ACID_MIDI_START:
        case ACID_MIDI_CONTINUE: s->start++; break;
        case ACID_MIDI_STOP: s->stop++; break;
        default: break;
        }
    }
}
