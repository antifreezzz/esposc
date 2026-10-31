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
    s.bsp_pos[2] = 55;
    s.held[(6 * 16 + 2) / 8] = (uint8_t)(1 << ((6 * 16 + 2) % 8));
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s, sizeof s, 20);
    CHECK(m.have && m.mode == 1 && m.bsp_page == 3);
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
    pl_model_local_set(&m, 6, 3, 30);
    CHECK(m.raw[6][3] == 30 && m.held[6][3] == 1);
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

int main(void)
{
    RUN(test_model_snapshot_replaces_all);
    RUN(test_knob_updates_value);
    RUN(test_toast_only_from_bsp);
    RUN(test_page_event);
    RUN(test_need_hello);
    RUN(test_local_set);
    RUN(test_follow_bsp_page);
    RUN(test_page_resend);
    RUN(test_link_lost);
    TEST_MAIN_END();
}
