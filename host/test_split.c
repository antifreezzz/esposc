// Рендер по ядрам: тарелки и перкуссия (side), синт 1 с ударкой (src) и смесь считаются раздельно,
// а звучат бит в бит как общий acid_synth_render_with_lr.
#include "acid.h"
#include "acid_demo.h"
#include "test.h"

#include <string.h>

#define SR 48000.0f
#define CH ACID_DEMO_CHUNK

static acid_synth_t a, b;
static acid_demo_t da, db;

// Песня кусками по CH, как прошивка: слот 1 на кусок вперёд не нужен, сравниваем только способ рендера.
static int same_as_whole(const acid_demo_song_t *song, int seconds)
{
    static acid_midi_event_t ev[96];
    static acid_synth_side_t side;
    static acid_synth_src_t src;
    float la[CH], ra[CH], lb[CH], rb[CH], s1[CH];
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    acid_demo_init(&da, song);
    acid_demo_init(&db, song);
    int same = 1;
    for (int k = 0; k < seconds * 48000 / CH; k++) {
        int n = acid_demo_events(&da, ev, 96);
        for (int i = 0; i < n; i++) {
            acid_synth_midi(&a, &ev[i]);
        }
        n = acid_demo_events(&db, ev, 96);
        for (int i = 0; i < n; i++) {
            acid_synth_midi(&b, &ev[i]);
        }
        acid_synth_render_slot(&a, 1, s1, CH);
        acid_synth_render_with_lr(&a, la, ra, CH, s1);
        acid_synth_render_slot(&b, 1, s1, CH);
        acid_synth_render_src(&b, &src, CH);
        acid_synth_render_side(&b, &side, CH);
        acid_synth_render_mix_lr(&b, lb, rb, CH, s1, &src, &side);
        same &= memcmp(la, lb, sizeof la) == 0 && memcmp(ra, rb, sizeof ra) == 0;
    }
    return same;
}

TEST(test_split_demo_bit_exact) { CHECK(same_as_whole(&acid_demo_song, 60)); }

TEST(test_split_stress_bit_exact) { CHECK(same_as_whole(&acid_stress_song, 10)); }

int main(void)
{
    RUN(test_split_demo_bit_exact);
    RUN(test_split_stress_bit_exact);
    TEST_MAIN_END();
}
