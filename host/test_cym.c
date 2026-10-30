#include "acid_cym.h"
#include "bank_fixture.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000

static uint32_t img_[2 * 1024 * 1024 / 4 + 1024]; // 2 МБ образа и канарейка после
static uint8_t *const img = (uint8_t *)img_;
static acid_bank_t bank;
static acid_cym_t c;

static const fixture_note_t NOTES[] = {
    {49, 3, {40, 90, 127}, {2, 2, 2}, SR},
    {51, 3, {40, 90, 127}, {1, 1, 1}, 3 * SR}, // райд длинный: голоса не кончаются сами за время теста
    {53, 3, {40, 90, 127}, {2, 2, 2}, SR},
};

static void setup(void)
{
    size_t n = fixture_bank(img, 2 * 1024 * 1024, NOTES, 3);
    memset(img + n, 0xAA, sizeof img_ - n); // за образом мусор: чтение туда испортит звук
    CHECK(n > 0 && acid_bank_open(&bank, img, n));
    acid_cym_init(&c, (float)SR);
    acid_cym_set_sens(&c, 1.0f);
    for (int w = 0; w < ACID_CYM_N; w++) {
        acid_cym_set(&c, w, 1.0f, 127, 64, 64);
    }
}

// Пик и энергия левого канала за n сэмплов.
static float run(int n, float *energy)
{
    float pk = 0.0f, e = 0.0f, l, r;
    for (int i = 0; i < n; i++) {
        acid_cym_next(&c, &l, &r);
        pk = fabsf(l) > pk ? fabsf(l) : pk;
        e += l * l;
    }
    if (energy) {
        *energy = e;
    }
    return pk;
}

static const acid_cym_voice_t *last_voice(void)
{
    const acid_cym_voice_t *best = NULL;
    for (int i = 0; i < ACID_CYM_SLOTS; i++) {
        const acid_cym_voice_t *v = &c.v[i];
        if (v->rr && v->fade_step == 0.0f && (!best || v->pos < best->pos)) {
            best = v;
        }
    }
    return best;
}

TEST(test_cym_layer_by_velocity)
{
    setup();
    acid_cym_trigger(&c, &bank, 49, 0.2f);
    CHECK(last_voice()->layer == 0);
    run(10, NULL);
    acid_cym_trigger(&c, &bank, 49, 0.5f);
    CHECK(last_voice()->layer == 1);
    run(10, NULL);
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    CHECK(last_voice()->layer == 2);
}

TEST(test_cym_sens_zero_full_layer)
{
    setup();
    acid_cym_set_sens(&c, 0.0f);
    acid_cym_trigger(&c, &bank, 49, 0.1f);
    CHECK(last_voice()->layer == 2);
    CHECK_NEAR(last_voice()->amp, 1.0f, 1e-6f);
    setup();
    acid_cym_trigger(&c, &bank, 49, 0.5f);
    CHECK_NEAR(last_voice()->amp, 0.25f, 1e-6f); // громкость - квадрат
}

TEST(test_cym_round_robin)
{
    setup();
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    int a = last_voice()->rr_index;
    run(10, NULL);
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    int b = last_voice()->rr_index;
    run(10, NULL);
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    CHECK(a != b);
    CHECK(last_voice()->rr_index == a);
}

TEST(test_cym_steal_quietest_fades)
{
    // Нот всего три, а повтор той же ноты уводит прежний голос: 6 звучащих собираем клонами голоса.
    setup();
    acid_cym_trigger(&c, &bank, 49, 0.1f);
    for (int i = 0; i < ACID_CYM_VOICES - 1; i++) {
        c.v[i + 1] = c.v[0]; // клон голоса: занятый звучащий слот
        c.v[i + 1].amp = 1.0f;
        c.v[i + 1].which = ACID_CYM_BELL;
    }
    c.v[0].which = ACID_CYM_CRASH;
    CHECK(acid_cym_sounding(&c) == ACID_CYM_VOICES);
    acid_cym_trigger(&c, &bank, 51, 1.0f);
    CHECK(acid_cym_sounding(&c) == ACID_CYM_VOICES);
    CHECK(c.v[0].rr != NULL && c.v[0].fade_step > 0.0f); // тихий уходит, а не обрывается
    run(SR / 1000, NULL);
    CHECK(c.v[0].rr != NULL); // через 1 мс ещё звучит
    run(SR / 100, NULL);
    CHECK(c.v[0].rr == NULL || c.v[0].fade_step == 0.0f); // через 10 мс ушёл
    CHECK(acid_cym_voices(&c) == ACID_CYM_VOICES);
}

TEST(test_cym_slots_full_no_crash)
{
    setup();
    for (int i = 0; i < 50; i++) {
        acid_cym_trigger(&c, &bank, 49 + 2 * (i % 3), 1.0f);
        CHECK(acid_cym_voices(&c) <= ACID_CYM_SLOTS);
        CHECK(acid_cym_sounding(&c) <= ACID_CYM_VOICES);
    }
    CHECK(isfinite(run(SR / 10, NULL)));
}

TEST(test_cym_retrigger_fades_previous)
{
    setup();
    acid_cym_trigger(&c, &bank, 51, 1.0f);
    run(SR / 5, NULL);
    acid_cym_trigger(&c, &bank, 51, 1.0f);
    CHECK(acid_cym_voices(&c) == 2);
    CHECK(acid_cym_sounding(&c) == 1);
    run(SR * 6 / 10, NULL);
    CHECK(acid_cym_voices(&c) == 2); // звон прежнего копится под новым: через 0.6 с ещё уходит
    run(SR * 15 / 100, NULL);
    CHECK(acid_cym_voices(&c) == 1); // за 0.7 с ушёл
}

TEST(test_cym_bell_keeps_ride)
{
    setup();
    acid_cym_trigger(&c, &bank, 51, 1.0f);
    run(100, NULL);
    acid_cym_trigger(&c, &bank, 53, 1.0f);
    run(SR / 10, NULL);
    CHECK(acid_cym_sounding(&c) == 2);
}

TEST(test_cym_tail_short)
{
    float e_long, e_short;
    setup();
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    run(SR / 2, NULL);
    run(SR * 4 / 10, &e_long);
    setup();
    acid_cym_set(&c, ACID_CYM_CRASH, 1.0f, 0, 64, 64);
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    run(SR / 2, NULL);
    run(SR * 4 / 10, &e_short);
    CHECK(e_long > 0.0f);
    CHECK(e_short < 0.05f * e_long);
}

static int life(int tune)
{
    setup();
    acid_cym_set(&c, ACID_CYM_CRASH, 1.0f, 127, tune, 64);
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    int n = 0;
    while (acid_cym_voices(&c) > 0 && n < 4 * SR) {
        run(1, NULL);
        n++;
    }
    return n;
}

TEST(test_cym_tune_octave)
{
    CHECK_NEAR(life(64), SR, SR * 0.01);
    CHECK_NEAR(life(127), SR / 2, SR * 0.05);
    CHECK_NEAR(life(0), SR * 2, SR * 0.1);
    CHECK_NEAR(life(1), SR * 2, SR * 0.1); // 1 зажата до -12 как 0
}

TEST(test_cym_tune_up_stops_at_end)
{
    setup();
    acid_cym_set(&c, ACID_CYM_CRASH, 1.0f, 127, 127, 64);
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    const acid_cym_voice_t *v = last_voice();
    const uint8_t *end = img + v->rr->offset + acid_bank_rr_bytes(v->rr->frames);
    uint32_t frames = v->rr->frames;
    float l, r;
    for (int i = 0; i < SR && v->rr; i++) {
        acid_cym_next(&c, &l, &r);
        CHECK(v->pos <= frames);
        CHECK(v->p <= end);
        CHECK(fabsf(l) < 1.0f);
    }
    CHECK(v->rr == NULL);
}

TEST(test_cym_pan_balance)
{
    float l0[1000], l64[1000], r0 = 0.0f, l, r;
    setup();
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    for (int i = 0; i < 1000; i++) {
        acid_cym_next(&c, &l64[i], &r);
    }
    setup();
    acid_cym_set(&c, ACID_CYM_CRASH, 1.0f, 127, 64, 0);
    acid_cym_trigger(&c, &bank, 49, 1.0f);
    for (int i = 0; i < 1000; i++) {
        acid_cym_next(&c, &l, &r);
        l0[i] = l;
        r0 += fabsf(r);
    }
    CHECK(r0 == 0.0f);
    CHECK(memcmp(l0, l64, sizeof l0) == 0);
    CHECK(fabsf(l64[500]) > 0.0f);
}

TEST(test_cym_ride_bell_follow_ride_knobs)
{
    setup();
    acid_cym_set(&c, ACID_CYM_RIDE, 0.5f, 10, 100, 20);
    CHECK(c.tail_coef[ACID_CYM_BELL] == c.tail_coef[ACID_CYM_RIDE]);
    CHECK(c.rate[ACID_CYM_BELL] == c.rate[ACID_CYM_RIDE]);
    CHECK(c.pan_l[ACID_CYM_BELL] == c.pan_l[ACID_CYM_RIDE] && c.pan_r[ACID_CYM_BELL] == c.pan_r[ACID_CYM_RIDE]);
    CHECK(c.gain[ACID_CYM_BELL] == 1.0f); // громкость купола своя
    acid_cym_set(&c, ACID_CYM_BELL, 0.3f, 127, 64, 64);
    CHECK(c.gain[ACID_CYM_BELL] == 0.3f);
    CHECK(c.rate[ACID_CYM_BELL] == c.rate[ACID_CYM_RIDE]); // строй купола по-прежнему от райда
}

TEST(test_cym_ride_roll_no_buildup)
{
    setup();
    int step = SR * 60 / 180 / 2; // восьмые на 180 BPM
    float first = 0.0f, second = 0.0f;
    for (int t = 0; t < 10 * SR; t += step) {
        acid_cym_trigger(&c, &bank, 51, 1.0f);
        float pk = run(step, NULL);
        CHECK(isfinite(pk));
        CHECK(acid_cym_voices(&c) <= 5); // перебитые уходят за 0.7 с: на восьмых не больше 4 под новым
        // Фикстура не затухает, а перебитые звенят под новыми: пик растёт первые ~0.7 с.
        // Накопление - это рост дальше: последние 5 с против секунд 1-3.
        if (t >= SR && t < 3 * SR) {
            first = pk > first ? pk : first;
        } else if (t > 5 * SR) {
            second = pk > second ? pk : second;
        }
    }
    CHECK(first > 0.0f);
    CHECK(second <= 1.05f * first);
}

// Бюджет платы (замер этапа 9): 8 слотов на строе 127 с двумя modal и всеми эффектами упирались в 2000 мкс.
TEST(test_cym_voice_budget)
{
    CHECK(ACID_CYM_SLOTS <= 6);
    CHECK(ACID_CYM_VOICES < ACID_CYM_SLOTS); // хоть один слот под уход, чтобы забор был без щелчка
    setup();
    for (int i = 0; i < 40; i++) {
        acid_cym_trigger(&c, &bank, 49 + 2 * (i % 3), 1.0f);
        run(SR / 20, NULL);
        CHECK(acid_cym_voices(&c) <= 6);
    }
}

TEST(test_cym_null_bank_silent)
{
    setup();
    acid_cym_trigger(&c, NULL, 49, 1.0f);
    acid_cym_trigger(&c, &bank, 50, 1.0f); // ноты нет в наборе
    CHECK(acid_cym_voices(&c) == 0);
    CHECK(run(1000, NULL) == 0.0f);
}

int main(void)
{
    RUN(test_cym_layer_by_velocity);
    RUN(test_cym_sens_zero_full_layer);
    RUN(test_cym_round_robin);
    RUN(test_cym_steal_quietest_fades);
    RUN(test_cym_slots_full_no_crash);
    RUN(test_cym_retrigger_fades_previous);
    RUN(test_cym_bell_keeps_ride);
    RUN(test_cym_tail_short);
    RUN(test_cym_tune_octave);
    RUN(test_cym_tune_up_stops_at_end);
    RUN(test_cym_pan_balance);
    RUN(test_cym_ride_bell_follow_ride_knobs);
    RUN(test_cym_ride_roll_no_buildup);
    RUN(test_cym_voice_budget);
    RUN(test_cym_null_bank_silent);
    TEST_MAIN_END();
}
