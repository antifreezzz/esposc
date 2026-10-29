#include "acid.h"
#include "test.h"

static acid_midi_event_t ev[8];

static int parse(const uint8_t *buf, size_t len)
{
    return acid_midi_parse(buf, len, ev, 8);
}

TEST(test_midi_note_on)
{
    const uint8_t p[] = {0x09, 0x94, 0x3C, 0x64};
    CHECK(parse(p, sizeof p) == 1);
    CHECK(ev[0].type == ACID_MIDI_NOTE_ON && ev[0].ch == 4 && ev[0].d1 == 60 && ev[0].d2 == 100);
}

TEST(test_midi_note_on_zero_velocity_is_off)
{
    const uint8_t p[] = {0x09, 0x94, 0x3C, 0x00};
    CHECK(parse(p, sizeof p) == 1);
    CHECK(ev[0].type == ACID_MIDI_NOTE_OFF && ev[0].ch == 4 && ev[0].d1 == 60);
}

TEST(test_midi_note_off)
{
    const uint8_t p[] = {0x08, 0x84, 0x3C, 0x40};
    CHECK(parse(p, sizeof p) == 1);
    CHECK(ev[0].type == ACID_MIDI_NOTE_OFF && ev[0].ch == 4 && ev[0].d1 == 60);
}

TEST(test_midi_cc)
{
    const uint8_t p[] = {0x0B, 0xB1, 0x4A, 0x7F};
    CHECK(parse(p, sizeof p) == 1);
    CHECK(ev[0].type == ACID_MIDI_CC && ev[0].ch == 1 && ev[0].d1 == 74 && ev[0].d2 == 127);
}

TEST(test_midi_stop)
{
    const uint8_t p[] = {0x0F, 0xFC, 0x00, 0x00};
    CHECK(parse(p, sizeof p) == 1);
    CHECK(ev[0].type == ACID_MIDI_STOP);
}

TEST(test_midi_ignores_other_messages)
{
    const uint8_t p[] = {
        0x0F, 0xFE, 0x00, 0x00, // active sensing
        0x04, 0xF0, 0x7F, 0x7F, // sysex start
        0x07, 0x06, 0x02, 0xF7, // sysex end
        0x0A, 0xA1, 0x24, 0x10, // poly aftertouch
        0x1F, 0xFC, 0x00, 0x00, // stop на кабеле 1: кабель не важен, это STOP
    };
    CHECK(parse(p, sizeof p) == 1);
    CHECK(ev[0].type == ACID_MIDI_STOP);
}

TEST(test_midi_clock_start_continue)
{
    const uint8_t p[] = {
        0x0F, 0xF8, 0x00, 0x00, // clock
        0x0F, 0xFA, 0x00, 0x00, // start
        0x0F, 0xFB, 0x00, 0x00, // continue
    };
    CHECK(parse(p, sizeof p) == 3);
    CHECK(ev[0].type == ACID_MIDI_CLOCK);
    CHECK(ev[1].type == ACID_MIDI_START);
    CHECK(ev[2].type == ACID_MIDI_CONTINUE);
}

TEST(test_midi_parse_multi_and_padding)
{
    const uint8_t p[] = {
        0x09, 0x91, 0x24, 0x7F,
        0x00, 0x00, 0x00, 0x00, // нулевой паддинг
        0x08, 0x81, 0x24, 0x00,
        0x0B, 0xB1, 0x0A, 0x05,
        0x00, 0x00, 0x00, 0x00,
        0x09, 0x94, // обрезанный хвост
    };
    CHECK(parse(p, sizeof p) == 3);
    CHECK(ev[0].type == ACID_MIDI_NOTE_ON && ev[0].d1 == 36);
    CHECK(ev[1].type == ACID_MIDI_NOTE_OFF && ev[1].d1 == 36);
    CHECK(ev[2].type == ACID_MIDI_CC && ev[2].d1 == 10 && ev[2].d2 == 5);
    CHECK(acid_midi_parse(p, sizeof p, ev, 2) == 2);
}

TEST(test_midi_masks_data_bytes)
{
    // Битый пакет: старшие биты данных не должны протечь в события.
    const uint8_t p[] = {0x0B, 0xB1, 0xCA, 0xFF, 0x09, 0x94, 0xBC, 0xE4};
    CHECK(parse(p, sizeof p) == 2);
    CHECK(ev[0].d1 == 74 && ev[0].d2 == 127);
    CHECK(ev[1].type == ACID_MIDI_NOTE_ON && ev[1].d1 == 60 && ev[1].d2 == 100);
}

int main(void)
{
    RUN(test_midi_note_on);
    RUN(test_midi_note_on_zero_velocity_is_off);
    RUN(test_midi_note_off);
    RUN(test_midi_cc);
    RUN(test_midi_stop);
    RUN(test_midi_ignores_other_messages);
    RUN(test_midi_clock_start_continue);
    RUN(test_midi_parse_multi_and_padding);
    RUN(test_midi_masks_data_bytes);
    TEST_MAIN_END();
}
