// Модель экрана панели: снимок, события ручек, всплывашка, запрос снимка.
#include "pl_model.h"
#include "test.h"

#include <string.h>

static pl_model_t m;

static void knob(int page, int enc, int value, int src, int pos, int held, uint32_t ms)
{
    pl_knob_t k = {(uint8_t)page, (uint8_t)enc, (uint8_t)value, (uint8_t)src, (uint8_t)pos, (uint8_t)held};
    pl_model_apply(&m, PL_KNOB, (const uint8_t *)&k, sizeof k, ms);
}

TEST(test_model_snapshot_replaces_all)
{
    pl_model_init(&m);
    CHECK(!m.have);
    knob(6, 2, 99, 0, 99, 0, 10);
    static pl_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.mode = 1;
    s.bsp_page = 3;
    for (int p = 0; p < PL_PAGES; p++) {
        for (int e = 0; e < PL_KNOBS; e++) {
            s.raw[p][e] = (uint8_t)(p * 10 + e);
        }
    }
    s.demo = 1;
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s, 20);
    static pl_snapshot2_t s2; // позиции энкодеров и подхват - вторым кадром
    memset(&s2, 0, sizeof s2);
    s2.bsp_pos[2] = 55;
    s2.held[(6 * 16 + 2) / 8] = (uint8_t)(1 << ((6 * 16 + 2) % 8));
    pl_model_apply(&m, PL_SNAPSHOT2, (const uint8_t *)&s2, sizeof s2, 20);
    CHECK(m.have && m.mode == 1 && m.bsp_page == 3);
    CHECK(m.demo == 1);
    CHECK(m.raw[6][2] == 62 && m.raw[9][15] == 105);
    CHECK(m.bsp_pos[2] == 55);
    CHECK(m.held[6][2] == 1 && m.held[6][3] == 0);
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s - 1, 30); // короткий - мусор
    CHECK(m.last_rx_ms == 20);
}

TEST(test_knob_updates_value)
{
    pl_model_init(&m);
    knob(8, 6, 3, 0, 3, 0, 100);
    CHECK(m.raw[8][6] == 3 && m.bsp_pos[6] == 3 && m.held[8][6] == 0);
    knob(8, 6, 30, 2, 87, 1, 110);
    CHECK(m.raw[8][6] == 30 && m.bsp_pos[6] == 87 && m.held[8][6] == 1);
    CHECK(m.last_rx_ms == 110);
}

TEST(test_toast_only_from_bsp)
{
    pl_model_init(&m);
    knob(0, 2, 50, 1, 0xFF, 0, 1000); // демо
    knob(0, 3, 50, 2, 0xFF, 0, 1000); // панель
    CHECK(m.toast_until_ms == 0);
    knob(6, 2, 93, 0, 93, 0, 2000);
    CHECK(m.toast_page == 6 && m.toast_enc == 2 && m.toast_until_ms == 3500);
    knob(0, 0, 4, 0, 0xFF, 0, 2100); // смена страницы BSP не всплывает
    CHECK(m.toast_page == 6);
}

TEST(test_page_event)
{
    pl_model_init(&m);
    knob(0, 0, 4, 0, 0xFF, 0, 5);
    CHECK(m.bsp_page == 4);
    CHECK(m.raw[0][0] == 0);
}

TEST(test_need_hello)
{
    pl_model_init(&m);
    CHECK(pl_model_need_hello(&m, 0));
    pl_model_hello_sent(&m, 0);
    CHECK(!pl_model_need_hello(&m, 100)); // ответ ещё может прийти
    CHECK(pl_model_need_hello(&m, 600));
    static pl_snapshot_t s;
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s, 1000);
    CHECK(!pl_model_need_hello(&m, 2500));
    CHECK(pl_model_need_hello(&m, 3100)); // 2 с тишины
}

TEST(test_local_set)
{
    pl_model_init(&m);
    CHECK(pl_model_local_set(&m, 6, 3, 30) == 0); // снимка нет: значение с нуля сбросило бы ручку синта
    CHECK(m.raw[6][3] == 0 && m.held[6][3] == 0);
    static pl_snapshot_t s;
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s, 0);
    CHECK(pl_model_local_set(&m, 6, 3, 30) == 1);
    CHECK(m.raw[6][3] == 30 && m.held[6][3] == 1);
    CHECK(pl_model_local_set(&m, 6, 0, 30) == 0); // энкодер 1 - страница, не ручка
}

TEST(test_toast_skips_empty_knob)
{
    pl_model_init(&m);
    knob(0, 15, 40, 0, 40, 0, 1000); // энкодер 16 на MIX пустой
    CHECK(m.toast_until_ms == 0);
    CHECK(m.raw[0][15] == 40 && m.bsp_pos[15] == 40); // положение BSP помним: нужно подхвату на других страницах
    knob(6, 11, 40, 0, 40, 0, 1000); // у 303 энкодер 12 пустой
    CHECK(m.toast_until_ms == 0);
}

TEST(test_page_choice_beats_bsp)
{
    pl_model_init(&m);
    knob(0, 0, 3, 0, 0xFF, 0, 5); // листали на BSP, пока палец на экране
    CHECK(m.goto_page == 3);
    pl_model_page_sent(&m, 10); // потом тап выбрал страницу: он новее
    CHECK(m.goto_page == -1);
}

TEST(test_follow_bsp_page)
{
    pl_model_init(&m);
    CHECK(m.goto_page == -1);
    knob(0, 0, 7, 1, 0xFF, 0, 5); // демо листает - экран не трогаем
    CHECK(m.goto_page == -1);
    knob(0, 0, 7, 2, 0xFF, 0, 5); // эхо своей страницы
    CHECK(m.goto_page == -1);
    knob(0, 0, 3, 0, 0xFF, 0, 6); // BSP
    CHECK(m.goto_page == 3);
}

static void live_snapshot(int bsp_page, uint32_t ms)
{
    static pl_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.mode = 1;
    s.bsp_page = (uint8_t)bsp_page;
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s, ms);
}

TEST(test_page_resend)
{
    pl_model_init(&m);
    CHECK(!pl_model_page_resend(&m, 6, 0)); // нет связи
    live_snapshot(5, 0);
    CHECK(pl_model_page_resend(&m, 5, 100) == 0); // синт уже там
    CHECK(pl_model_page_resend(&m, 6, 100) == 1); // экран на 6, синт на 5
    pl_model_page_sent(&m, 100);
    CHECK(pl_model_page_resend(&m, 6, 200) == 0); // ответ ещё в пути
    CHECK(pl_model_page_resend(&m, 6, 450) == 1); // кадр потерялся - ещё раз
    knob(0, 0, 3, 0, 0xFF, 0, 460);               // BSP листает: экран идёт за ним, не перебиваем
    CHECK(pl_model_page_resend(&m, 6, 900) == 0);
    m.goto_page = -1;
    static pl_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.bsp_page = 2; // демо
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s, 1000);
    CHECK(pl_model_page_resend(&m, 6, 2000) == 0); // в демо страницу экрана синт не берёт
}

TEST(test_link_lost)
{
    pl_model_init(&m);
    CHECK(!pl_model_linked(&m, 0));
    live_snapshot(6, 1000);
    CHECK(pl_model_linked(&m, 5000));
    CHECK(!pl_model_linked(&m, 8000)); // 7 с тишины при HELLO раз в 2 с - синта нет
    CHECK(m.raw[6][0] == 0 && m.have); // значения не стираем
    live_snapshot(6, 9000);
    CHECK(pl_model_linked(&m, 9000));
}

static void linked(void)
{
    static pl_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.mode = 1;
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s, 1);
}

static void custom(int up, int enc, int page, int tenc, int lo, int hi, int val)
{
    static pl_custom_t c;
    memset(&c, PL_NONE, sizeof c);
    memset(c.val, 0, sizeof c.val);
    memset(c.held, 0, sizeof c.held);
    c.up = (uint8_t)up;
    c.t[enc - 1][0] = (pl_target_t){(uint8_t)(page << 4 | tenc), (uint8_t)lo, (uint8_t)hi};
    c.val[enc - 1] = (uint8_t)val;
    c.held[(enc - 1) / 8] = (uint8_t)(1 << ((enc - 1) % 8));
    pl_model_apply(&m, PL_CUSTOM, (const uint8_t *)&c, sizeof c, 5);
}

TEST(test_custom_frame_applied)
{
    pl_model_init(&m);
    linked();
    custom(2, 3, 6, 2, 10, 90, 44);
    CHECK(m.user_have == (1 << 2));
    CHECK(m.user_t[2][3][0].pe == (6 << 4 | 2) && m.user_t[2][3][0].lo == 10 && m.user_t[2][3][0].hi == 90);
    CHECK(m.user_t[2][3][1].pe == PL_NONE);
    CHECK(m.user_val[2][3] == 44 && m.user_held[2][3] == 1 && m.user_held[2][4] == 0);
    CHECK(m.last_rx_ms == 5);
}

TEST(test_custom_wrong_len_skipped)
{
    pl_model_init(&m);
    static pl_custom_t c;
    memset(&c, 0, sizeof c);
    c.up = 9; // нет такой страницы
    pl_model_apply(&m, PL_CUSTOM, (const uint8_t *)&c, sizeof c, 5);
    CHECK(m.user_have == 0);
    c.up = 0;
    pl_model_apply(&m, PL_CUSTOM, (const uint8_t *)&c, sizeof c - 1, 6);
    CHECK(m.user_have == 0 && m.last_rx_ms == 5); // короткий кадр не считается
}

TEST(test_user_knob_and_page_change)
{
    pl_model_init(&m);
    linked();
    custom(0, 2, 6, 2, 0, 50, 0);
    knob(PL_USER_FIRST, 2, 70, 0, 70, 0, 100);
    CHECK(m.user_val[0][2] == 70 && m.user_held[0][2] == 0 && m.bsp_pos[2] == 70);
    CHECK(m.toast_page == PL_USER_FIRST && m.toast_enc == 2 && m.toast_until_ms == 100 + PL_TOAST_MS);
    knob(PL_USER_FIRST, 0, PL_USER_FIRST + 2, 0, 0, 0, 110); // BSP открыл свою страницу 3
    CHECK(m.bsp_page == PL_USER_FIRST + 2 && m.goto_page == PL_USER_FIRST + 2);
    knob(PL_USER_FIRST + PL_USER_PAGES, 2, 1, 0, 0, 0, 120); // нет такой страницы
    CHECK(m.user_val[0][2] == 70);
    m.toast_until_ms = 0;
    knob(PL_USER_FIRST, 5, 9, 0, 9, 0, 130); // пустой слот не всплывает
    CHECK(m.user_val[0][5] == 9 && m.toast_until_ms == 0);
}

TEST(test_local_set_user_needs_frame)
{
    pl_model_init(&m);
    linked();
    CHECK(pl_model_local_set(&m, PL_USER_FIRST + 1, 2, 40) == 0); // определений ещё нет
    custom(1, 2, 0, 1, 0, 127, 0);
    CHECK(pl_model_local_set(&m, PL_USER_FIRST + 1, 2, 40) == 1);
    CHECK(m.user_val[1][2] == 40 && m.user_held[1][2] == 1);
    CHECK(pl_model_local_set(&m, PL_USER_FIRST + PL_USER_PAGES, 2, 40) == 0);
}

TEST(test_local_set_alias_moves_target)
{
    pl_model_init(&m);
    linked();
    custom(1, 2, 6, 3, 0, 127, 0); // псевдоним на синт 1, энкодер 4
    CHECK(pl_model_local_set(&m, PL_USER_FIRST + 1, 2, 40) == 1);
    CHECK(m.raw[6][3] == 40); // экран показывает цель сразу, без ответа синта
    custom(1, 2, 6, 3, 10, 50, 0); // макро
    m.raw[6][3] = 7;
    CHECK(pl_model_local_set(&m, PL_USER_FIRST + 1, 2, 90) == 1);
    CHECK(m.raw[6][3] == 7 && m.user_val[1][2] == 90); // цели макро двигает синт
}

TEST(test_scene_state_applied)
{
    pl_model_init(&m);
    pl_scene_state_t st = {0x05, 2, 2};
    pl_model_apply(&m, PL_SCENE_STATE, (const uint8_t *)&st, sizeof st, 7);
    CHECK(m.scene.used == 0x05 && m.scene.cur == 2 && m.scene.state == 2);
    CHECK(m.last_rx_ms == 7);
}

TEST(test_load_frame_stored)
{
    pl_model_init(&m);
    CHECK(!m.have_load);
    pl_load_t l = {75, 90, 50, 95, 2, 1};
    pl_model_apply(&m, PL_LOAD, (const uint8_t *)&l, sizeof l, 1234);
    CHECK(m.have_load);
    CHECK(m.load_ms == 1234);
    CHECK(m.last_rx_ms == 1234);
    CHECK(memcmp(&m.load, &l, sizeof l) == 0);
}

TEST(test_load_wrong_len_ignored)
{
    pl_model_init(&m);
    uint8_t b[5] = {1, 2, 3, 4, 5};
    pl_model_apply(&m, PL_LOAD, b, sizeof b, 50);
    CHECK(!m.have_load);
}

TEST(test_grids_step_frame)
{
    pl_model_init(&m);
    CHECK(m.grids_step == -1);
    uint8_t b = 21;
    pl_model_apply(&m, PL_GRIDS_STEP, &b, 1, 500);
    CHECK(m.grids_step == 21 && m.last_rx_ms == 500);
    b = 0xFF;
    pl_model_apply(&m, PL_GRIDS_STEP, &b, 1, 510);
    CHECK(m.grids_step == -1);
}

TEST(test_seq_dump_pos_and_local_edits)
{
    pl_model_t m;
    pl_model_init(&m);
    CHECK(pl_model_seq_step(&m, 1, 2, 3, 5) == 0); // дампа ещё нет
    static pl_seq_dump_t d;
    memset(&d, 0, sizeof d);
    d.synth = 1;
    d.pat[2][3] = PL_SEQ_REST;
    d.bpm = 133;
    pl_model_apply(&m, PL_SEQ_DUMP, (const uint8_t *)&d, sizeof d, 10);
    CHECK(m.seq_have == 2);
    CHECK(m.seq[1].bpm == 133 && m.seq[1].pat[2][3] == PL_SEQ_REST);
    CHECK(pl_model_seq_step(&m, 1, 2, 3, 5 | PL_SEQ_ACC) == 1);
    CHECK(m.seq[1].pat[2][3] == (5 | PL_SEQ_ACC));
    CHECK(pl_model_seq_step(&m, 0, 2, 3, 5) == 0); // синт 0 без дампа
    CHECK(pl_model_seq_oct(&m, 1, 2, -1) == 1 && m.seq[1].oct[2] == 1); // как в кадре: oct + 2
    uint8_t cells[8] = {0, 0, 0, 1, PL_SEQ_EMPTY, PL_SEQ_EMPTY, PL_SEQ_EMPTY, PL_SEQ_EMPTY};
    CHECK(pl_model_seq_row(&m, 1, 0, cells, 3) == 1 && m.seq[1].cell[0][3] == 1 && m.seq[1].rep[0] == 3);
    CHECK(pl_model_seq_ctrl(&m, 1, 1, 4) == 1 && m.seq[1].run == 1 && m.seq[1].edit == 4);
    CHECK(pl_model_seq_ctrl(&m, 1, 0, 4) == 1 && m.seq[1].run == 0);
    CHECK(pl_model_seq_ctrl(&m, 1, PL_SEQ_KEEP, 6) == 1 && m.seq[1].run == 0 && m.seq[1].edit == 6); // такт без ▶/■
    CHECK(pl_model_seq_ctrl(&m, 1, 1, 4) == 1);
    CHECK(pl_model_seq_bpm(&m, 150) == 1 && m.seq[0].bpm == 150 && m.seq[1].bpm == 150);
    pl_seq_pos_t p = {1, 4, 7, 0, 3, 2, 0, 99};
    pl_model_apply(&m, PL_SEQ_POS, (const uint8_t *)&p, sizeof p, 20);
    CHECK(m.seq_pos[1].step == 7 && m.seq_pos[1].bpm == 99);
    CHECK(m.seq[1].run == 0); // синт остановил (Stop BSP): экран видит
    pl_model_apply(&m, PL_SEQ_DUMP, (const uint8_t *)&d, sizeof d - 1, 30); // чужая длина - мимо
    CHECK(m.seq[1].pat[2][3] == (5 | PL_SEQ_ACC));
}

int main(void)
{
    RUN(test_load_frame_stored);
    RUN(test_grids_step_frame);
    RUN(test_load_wrong_len_ignored);
    RUN(test_model_snapshot_replaces_all);
    RUN(test_knob_updates_value);
    RUN(test_toast_only_from_bsp);
    RUN(test_page_event);
    RUN(test_need_hello);
    RUN(test_local_set);
    RUN(test_toast_skips_empty_knob);
    RUN(test_page_choice_beats_bsp);
    RUN(test_follow_bsp_page);
    RUN(test_page_resend);
    RUN(test_link_lost);
    RUN(test_custom_frame_applied);
    RUN(test_custom_wrong_len_skipped);
    RUN(test_user_knob_and_page_change);
    RUN(test_local_set_user_needs_frame);
    RUN(test_local_set_alias_moves_target);
    RUN(test_scene_state_applied);
    RUN(test_seq_dump_pos_and_local_edits);
    TEST_MAIN_END();
}
