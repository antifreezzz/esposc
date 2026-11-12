#include "acid.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000.0f
#define BASS 4  // MIDI канал 5
#define SEQ2 5  // MIDI канал 6
#define FM2 11  // тип: модель Plaits 10, FM 2-op
#define BLK ACID_PLAITS_BLOCK

static acid_synth_t s;
static float buf[48000];
static float last; // последний отрендеренный сэмпл

static const uint8_t ENC[17] = {0, 10, 74, 71, 76, 77, 93, 73, 75, 114, 18, 19, 16, 17, 91, 79, 72};

static void live(void)
{
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
}

static void send(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2)
{
    acid_midi_event_t e = {.type = type, .ch = ch, .d1 = d1, .d2 = d2};
    acid_synth_midi(&s, &e);
}

static void on(uint8_t ch, uint8_t note, uint8_t vel) { send(ACID_MIDI_NOTE_ON, ch, note, vel); }
static void off(uint8_t ch, uint8_t note) { send(ACID_MIDI_NOTE_OFF, ch, note, 0); }
static void cc(int enc, int val) { send(ACID_MIDI_CC, 1, ENC[enc], (uint8_t)val); }
static void page(int p) { cc(1, p); } // значение энкодера 1 - номер страницы

static float rms(int n)
{
    acid_synth_render(&s, buf, (size_t)n);
    last = buf[n - 1];
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        sq += (double)buf[i] * buf[i];
    }
    return (float)sqrt(sq / n);
}

// Тип синтеза энкодером 2 на текущей странице синта, с ожиданием затухания и прихода.
static void type(int t)
{
    cc(2, t);
    rms(960);
}

static float slot_hz(int k)
{
    return exp2f(s.slot[k].pitch_log2);
}

TEST(test_slot_type_is_screen_value)
{
    type(3);
    CHECK(s.slot[0].type == 3);
    type(24);
    CHECK(s.slot[0].type == 24);
    type(26); // вне списка (25 - PETRI, 26 - «выкл» только у слоя): игнор
    CHECK(s.slot[0].type == 24);
    type(127);
    CHECK(s.slot[0].type == 24);
    type(0);
    CHECK(s.slot[0].type == 0);
}

TEST(test_slot_defaults)
{
    live();
    CHECK(s.slot[0].type == 0);
    CHECK(s.slot[1].type == FM2);
}

TEST(test_slot_type_select)
{
    live();
    page(6);
    type(24);
    CHECK(s.slot[0].type == 24);
    type(FM2);
    CHECK(s.slot[0].type == FM2);
    type(0);
    CHECK(s.slot[0].type == 0);
    CHECK(s.slot[1].type == FM2); // другой синт не задет
}

TEST(test_slot_plaits_plays)
{
    live();
    page(6);
    type(FM2);
    CHECK(s.slot[0].type == FM2);
    cc(4, 0); // timbre 0: FM без модуляции, чистый синус
    on(BASS, 45, 100);
    rms(4800);
    CHECK(rms(48000) > 0.01f);
    int zc = 0;
    for (int i = 1; i < 48000; i++) {
        zc += (buf[i - 1] < 0.0f) != (buf[i] < 0.0f);
    }
    CHECK_NEAR(zc / 2.0f, 110.0f, 2.0f);
    off(BASS, 45);
    rms(48000);
    CHECK(rms(4800) < 1e-3f);
}

TEST(test_slot_plaits_level)
{
    // Plaits и 303 на умолчаниях звучат сопоставимо по громкости.
    live();
    on(BASS, 45, 100);
    rms(2400);
    float r303 = rms(24000);
    live();
    page(6);
    type(FM2);
    CHECK(s.slot[0].type == FM2);
    on(BASS, 45, 100);
    rms(2400);
    float rfm = rms(24000);
    CHECK(rfm > r303 * 0.5f && rfm < r303 * 2.0f);
}

// Рендер по блокам Plaits, бит 1 в mask[i] - триггер в блоке i.
static void trace_trigger(int blocks, int *lows, int *highs)
{
    *lows = *highs = 0;
    for (int b = 0; b < blocks; b++) {
        rms(BLK);
        if (s.slot[0].pp.trigger) {
            (*highs)++;
        } else {
            (*lows)++;
        }
    }
}

TEST(test_plaits_slot_retrigger)
{
    // Note off и note on в одном куске MIDI: триггер опускается и снова поднимается.
    live();
    page(6);
    type(FM2);
    on(BASS, 45, 100);
    rms(4800);
    off(BASS, 45);
    on(BASS, 47, 100);
    int lows, highs;
    trace_trigger(40, &lows, &highs);
    CHECK(lows >= 1);
    CHECK(highs >= 30);
    CHECK(s.slot[0].pp.trigger == 1);
}

TEST(test_plaits_slot_slide)
{
    live();
    page(6);
    type(FM2);
    on(BASS, 45, 100);
    rms(4800);
    on(BASS, 57, 100); // перекрытие: слайд без нового триггера
    int lows, highs;
    trace_trigger(40, &lows, &highs);
    CHECK(lows == 0);
    float hz = slot_hz(0);
    CHECK(hz > 111.0f && hz < 218.0f); // ещё ползёт
    rms(24000);
    CHECK_NEAR(slot_hz(0), 220.0f, 1.0f);
}

static float max_jump(int n)
{
    float prev = last;
    acid_synth_render(&s, buf, (size_t)n);
    last = buf[n - 1];
    float j = 0.0f;
    for (int i = 0; i < n; i++) {
        j = fabsf(buf[i] - prev) > j ? fabsf(buf[i] - prev) : j;
        prev = buf[i];
    }
    return j;
}

// Переход с текущего типа на t: скачки во время ухода и прихода не больше, чем в самих звуках.
static void check_switch(int t)
{
    rms(4800);
    float before = max_jump(960);
    cc(2, t);
    float during = max_jump(2 * 192 + 48);
    CHECK(s.slot[0].type == t);
    float after = max_jump(960);
    float ref = before > after ? before : after;
    CHECK(during < ref * 1.2f + 0.005f);
}

TEST(test_slot_type_switch_no_click)
{
    live();
    page(6);
    on(BASS, 45, 127);
    check_switch(FM2);
    check_switch(0); // и обратно, с Plaits на 303
}

TEST(test_slot_knob_memory)
{
    live();
    page(6);
    cc(3, 0); // 303: cutoff
    CHECK_NEAR(s.slot[0].cut_base, 60.0f, 0.1f);
    type(FM2);
    cc(3, 100); // Plaits: harmonics
    CHECK_NEAR(s.slot[0].pp.harmonics, 100 / 127.0f, 1e-4f);
    type(0);
    CHECK_NEAR(s.slot[0].cut_base, 60.0f, 0.1f);
    type(FM2);
    CHECK_NEAR(s.slot[0].pp.harmonics, 100 / 127.0f, 1e-4f);
    type(FM2 + 1); // эту модель не трогали: умолчания, а не ручки FM
    CHECK_NEAR(s.slot[0].pp.harmonics, 0.5f, 1e-6f);
    page(7);
    CHECK_NEAR(s.slot[1].pp.harmonics, 0.5f, 1e-6f); // синт 2 со своей памятью
}

// Ручка сразу после смены типа (тип ещё уходит) - выбранному типу, как на экране, а не уходящему.
TEST(test_slot_knob_during_switch)
{
    live();
    page(6);
    cc(3, 0); // 303: cutoff
    cc(2, FM2);
    cc(3, 100); // уже Plaits: harmonics
    CHECK(s.slot[0].type == 0);
    CHECK_NEAR(s.slot[0].cut_base, 60.0f, 0.1f); // 303 не тронут
    rms(960);
    CHECK(s.slot[0].type == FM2);
    CHECK_NEAR(s.slot[0].pp.harmonics, 100 / 127.0f, 1e-4f);
}

TEST(test_slot_plaits_page)
{
    live();
    page(6);
    type(FM2);
    acid_slot_t *v = &s.slot[0];
    cc(3, 127);
    CHECK_NEAR(v->pp.harmonics, 1.0f, 1e-6f);
    cc(4, 0);
    CHECK_NEAR(v->pp.timbre, 0.0f, 1e-6f);
    cc(5, 127);
    CHECK_NEAR(v->pp.morph, 1.0f, 1e-6f);
    cc(6, 0);
    CHECK_NEAR(v->pp.decay, 0.0f, 1e-6f);
    cc(7, 127);
    CHECK_NEAR(v->pp.lpg_colour, 1.0f, 1e-6f);
    cc(10, 0);
    CHECK_NEAR(v->pp.timbre_env, -1.0f, 1e-6f);
    cc(10, 64);
    CHECK_NEAR(v->pp.timbre_env, 0.0f, 1e-6f);
    cc(11, 127);
    CHECK_NEAR(v->pp.morph_env, 1.0f, 1e-6f);
    cc(12, 127);
    CHECK_NEAR(v->pp.fm_env, 1.0f, 1e-6f);

    cc(13, 127); // сильный accent: обычная нота заметно тише
    on(BASS, 45, 100);
    rms(BLK * 2);
    float normal = v->pp.level;
    off(BASS, 45);
    rms(4800);
    on(BASS, 45, 127);
    rms(BLK * 2);
    CHECK_NEAR(v->pp.level, 1.0f, 1e-6f);
    CHECK(normal < 0.5f);
    off(BASS, 45);
    rms(4800);

    cc(9, 76); // транспонирование +12
    cc(8, 0);   // самый быстрый глайд
    on(BASS, 45, 100);
    rms(480);
    CHECK_NEAR(slot_hz(0), 220.0f, 1.0f);
}

TEST(test_slot_idle_plaits_silent)
{
    // Молчащий синт Plaits даёт точные нули (и не тратит процессор).
    live();
    acid_synth_render_slot(&s, 1, buf, 4800);
    int zeros = 1;
    for (int i = 0; i < 4800; i++) {
        zeros &= buf[i] == 0.0f;
    }
    CHECK(zeros);
}

// После отпущенной ноты голос Plaits засыпает (не считается), какой бы ни была модель.
TEST(test_slot_plaits_sleeps_after_note)
{
    for (int t = 1; t <= ACID_PLAITS_MODELS; t++) {
        live();
        page(7);
        type(t);
        on(SEQ2, 48, 100);
        acid_synth_render_slot(&s, 1, buf, 4800);
        off(SEQ2, 48);
        for (int i = 0; i < 10; i++) {
            acid_synth_render_slot(&s, 1, buf, 48000); // 10 с хвоста
        }
        if (s.slot[1].idle < 400) { // IDLE_BLOCKS
            printf("  type %d: idle %d\n", t, s.slot[1].idle);
            CHECK(0);
        }
    }
}

// 303 после отпущенной ноты засыпает: выход ровно 0, осциллятор и фильтр не считаются; новая нота звучит.
TEST(test_slot_303_sleeps_after_note)
{
    live();
    on(BASS, 45, 100);
    rms(4800);
    off(BASS, 45);
    rms(48000);
    float ph = s.slot[0].osc.phase;
    float r = rms(4800);
    CHECK(r == 0.0f);
    CHECK(s.slot[0].osc.phase == ph);
    on(BASS, 45, 100);
    CHECK(rms(4800) > 0.01f);
}

TEST(test_slot_save_flag)
{
    live();
    CHECK(acid_synth_take_save(&s) == 0);
    page(6);
    cc(3, 10);
    rms(960);
    CHECK(acid_synth_take_save(&s) == 1); // любая ручка - пора записать, иначе правка теряется
    CHECK(acid_synth_take_save(&s) == 0);
    type(FM2);
    CHECK(acid_synth_take_save(&s) == 1);
    CHECK(acid_synth_take_save(&s) == 0);
    page(0);
    cc(2, 70); // микс
    CHECK(acid_synth_take_save(&s) == 1);
    acid_synth_set_knob(&s, 8, 3, 50); // панель, сцена
    CHECK(acid_synth_take_save(&s) == 1);
    page(6);
    cc(2, 26); // тип вне списка: ничего не поменялось
    CHECK(acid_synth_take_save(&s) == 0);
}

TEST(test_slot_export_import)
{
    static acid_synth_t b;
    live();
    page(6);
    cc(3, 0);
    type(5);
    cc(4, 127);
    page(7);
    type(0);
    cc(4, 0);
    static uint8_t blob[ACID_SYNTH_BLOB];
    CHECK(acid_synth_export(&s, blob, sizeof blob) == ACID_SYNTH_BLOB);

    acid_synth_init(&b, SR);
    acid_synth_notes_off(&b);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 1);
    CHECK(b.slot[0].type == 5);
    CHECK_NEAR(b.slot[0].pp.timbre, 1.0f, 1e-6f);
    CHECK(b.slot[1].type == 0);
    CHECK_NEAR(b.slot[1].res, 0.0f, 1e-6f);
    blob[0] ^= 0xFF; // чужой или битый blob не применяется
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 0);
}

// Ручки страницы 5 (слой тональности) уходят в blob и возвращаются; CC страницы 5 просит записи.
TEST(test_tonal_blob_roundtrip)
{
    static acid_synth_t b;
    live();
    acid_synth_take_save(&s);
    page(9);
    CHECK(acid_synth_take_save(&s) == 0);
    cc(2, 2);   // длина 2 такта
    CHECK(acid_synth_take_save(&s) == 1);
    cc(7, 3);   // дорийский
    cc(12, 70); // шаг 4 = +6
    static uint8_t blob[ACID_SYNTH_BLOB];
    CHECK(acid_synth_export(&s, blob, sizeof blob) == ACID_SYNTH_BLOB);
    acid_synth_init(&b, SR);
    acid_synth_notes_off(&b);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 1);
    CHECK(b.tonal.len == 2);
    CHECK(b.tonal.scale == 3);
    CHECK(b.tonal.raw[3] == 6);
    CHECK(b.tonal.nsteps == 4); // не трогали - умолчание
}

// Blob прежней версии (магия AS5 1) не применяется.
TEST(test_blob_old_magic_rejected)
{
    static acid_synth_t b;
    live();
    static uint8_t blob[ACID_SYNTH_BLOB];
    acid_synth_export(&s, blob, sizeof blob);
    const uint8_t old[4] = {'A', 'S', '5', 1};
    memcpy(blob, old, sizeof old);
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 0);
}

TEST(test_slot_import_applies_type)
{
    // Сохранённый выбор применяется сразу при загрузке, и у синта 1 тоже.
    static uint8_t blob[ACID_SYNTH_BLOB];
    static acid_synth_t b;
    live();
    page(6);
    type(FM2);
    acid_synth_export(&s, blob, sizeof blob);
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, sizeof blob));
    CHECK(b.slot[0].type == FM2);
}

// Страницы без своих knob[] синтов (микс, ударка, тарелки, перкуссия, эффекты, Grids, чашка) - тоже в blob.
static const int PAGES_V5[] = {0, 1, 2, 3, 4, 5, 8, ACID_PAGE_GRIDS, ACID_PAGE_PETRI};
#define NPAGES_V5 ((int)(sizeof PAGES_V5 / sizeof PAGES_V5[0]))

// Исполнительские ручки (статтер, фриз чашки, сбивка Grids сейчас) после перезагрузки не включаются сами.
static int performance_knob(int page, int enc)
{
    return (page == 8 && enc == 9) || (page == ACID_PAGE_PETRI && enc == 14) ||
           (page == ACID_PAGE_GRIDS && enc == DRM_FILL_NOW);
}

TEST(test_blob_v5_all_pages_roundtrip)
{
    static acid_synth_t b;
    live();
    acid_synth_set_knob(&s, 0, 1, 10);    // мастер
    acid_synth_set_knob(&s, 0, 13, 20);   // панорама синта 1
    acid_synth_set_knob(&s, 1, 1, 100);   // тон бочки
    acid_synth_set_knob(&s, 2, 10, 5);    // чувствительность тарелок
    acid_synth_set_knob(&s, 3, 2, 90);    // томы: тон
    acid_synth_set_knob(&s, 5, 1, 2);     // ковбелл: модель
    acid_synth_set_knob(&s, 8, 3, 77);    // посыл синта 1
    acid_synth_set_knob(&s, 8, 6, 3);     // время дилэя
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_X, 99);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_ROLL, 55);
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 2, 30);
    static uint8_t blob[ACID_SYNTH_BLOB];
    CHECK(acid_synth_export(&s, blob, sizeof blob) == ACID_SYNTH_BLOB);
    CHECK(blob[3] == 8);

    acid_synth_init(&b, SR);
    acid_synth_notes_off(&b);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 1);
    int same = 1;
    for (int i = 0; i < NPAGES_V5; i++) {
        for (int e = 1; e < ACID_KNOBS; e++) {
            if (acid_synth_knob(&b, PAGES_V5[i], e) != acid_synth_knob(&s, PAGES_V5[i], e)) {
                printf("  page %d enc %d: %d != %d\n", PAGES_V5[i], e, acid_synth_knob(&b, PAGES_V5[i], e),
                       acid_synth_knob(&s, PAGES_V5[i], e));
                same = 0;
            }
        }
    }
    CHECK(same);
    CHECK(acid_synth_knob(&b, 0, 1) == 10);
    CHECK_NEAR(b.master, s.master, 1e-6f);
    CHECK_NEAR(b.kick_tune, s.kick_tune, 1e-6f);
    CHECK_NEAR(b.send1, s.send1, 1e-6f);
    CHECK_NEAR(b.pan_l[ACID_PAN_SYNTH1], s.pan_l[ACID_PAN_SYNTH1], 1e-6f);
    CHECK(b.perc.inst[2].model == 2);
    CHECK(b.dl_time == 3);
    CHECK(b.drummer.knob[DRM_ROLL] == 55);
    CHECK(acid_synth_take_save(&b) == 0); // загрузка - не правка пользователя
}

TEST(test_blob_v5_skips_performance_knobs)
{
    static acid_synth_t b;
    live();
    acid_synth_set_knob(&s, 8, 9, 3);                 // статтер
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 14, 100); // фриз
    static uint8_t blob[ACID_SYNTH_BLOB];
    acid_synth_export(&s, blob, sizeof blob);
    acid_synth_init(&b, SR);
    acid_synth_notes_off(&b);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 1);
    for (int i = 0; i < NPAGES_V5; i++) {
        for (int e = 1; e < ACID_KNOBS; e++) {
            if (performance_knob(PAGES_V5[i], e)) {
                static acid_synth_t d;
                acid_synth_init(&d, SR);
                CHECK(acid_synth_knob(&b, PAGES_V5[i], e) == acid_synth_knob(&d, PAGES_V5[i], e));
            }
        }
    }
}

// Blob версии 4 (только синты, слои, тональность) применяется: прочие страницы - умолчания.
TEST(test_blob_v4_still_imports)
{
    static acid_synth_t b;
    live();
    page(6);
    type(FM2);
    acid_synth_set_knob(&s, 0, 1, 10);
    static uint8_t blob[ACID_SYNTH_BLOB];
    acid_synth_export(&s, blob, sizeof blob);
    blob[3] = 4;
    size_t v4 = 4 + 2 * ACID_SLOTS * (1 + ACID_TYPES * ACID_KNOBS) + ACID_TONAL_KNOBS;
    acid_synth_init(&b, SR);
    acid_synth_notes_off(&b);
    CHECK(acid_synth_import(&b, blob, v4) == 1);
    CHECK(b.slot[0].type == FM2);
    static acid_synth_t d;
    acid_synth_init(&d, SR);
    CHECK(acid_synth_knob(&b, 0, 1) == acid_synth_knob(&d, 0, 1));
}

int main(void)
{
    RUN(test_slot_defaults);
    RUN(test_slot_type_is_screen_value);
    RUN(test_slot_type_select);
    RUN(test_slot_plaits_plays);
    RUN(test_slot_plaits_level);
    RUN(test_plaits_slot_retrigger);
    RUN(test_plaits_slot_slide);
    RUN(test_slot_type_switch_no_click);
    RUN(test_slot_knob_memory);
    RUN(test_slot_knob_during_switch);
    RUN(test_slot_plaits_page);
    RUN(test_slot_idle_plaits_silent);
    RUN(test_slot_plaits_sleeps_after_note);
    RUN(test_slot_303_sleeps_after_note);
    RUN(test_slot_save_flag);
    RUN(test_slot_export_import);
    RUN(test_tonal_blob_roundtrip);
    RUN(test_blob_old_magic_rejected);
    RUN(test_slot_import_applies_type);
    RUN(test_blob_v5_all_pages_roundtrip);
    RUN(test_blob_v5_skips_performance_knobs);
    RUN(test_blob_v4_still_imports);
    TEST_MAIN_END();
}
