// Сверка эха канала синт <-> панель: сборка строк из байтов и учёт потерь.
#include <string.h>

#include "../../../host/test.h"
#include "link_check.h"

static void feed(link_check_t *c, const char *s) {
    for (; *s; s++) link_check_byte(c, (unsigned char)*s);
}

TEST(test_in_order_replies_count_ok) {
    link_check_t c;
    link_check_init(&c);
    feed(&c, "ping 0\nping 1\nping 2\n");
    CHECK(c.ok == 3);
    CHECK(c.lost == 0);
    CHECK(c.bad == 0);
}

TEST(test_line_split_across_reads) {
    link_check_t c;
    link_check_init(&c);
    feed(&c, "pi");
    CHECK(c.ok == 0);
    feed(&c, "ng 0\n");
    CHECK(c.ok == 1);
}

TEST(test_gap_in_sequence_counts_lost) {
    link_check_t c;
    link_check_init(&c);
    feed(&c, "ping 0\nping 3\nping 4\n");
    CHECK(c.ok == 3);
    CHECK(c.lost == 2);
}

TEST(test_garbage_counts_bad_and_keeps_sequence) {
    link_check_t c;
    link_check_init(&c);
    feed(&c, "ping 0\npi\xffg 1\nping 2\n");
    CHECK(c.ok == 2);
    CHECK(c.bad == 1);
    // испорченная строка 1 уже посчитана как bad, в lost она не идёт
    CHECK(c.lost == 0);
}

TEST(test_repeat_or_old_number_is_bad) {
    link_check_t c;
    link_check_init(&c);
    feed(&c, "ping 0\nping 1\nping 1\n");
    CHECK(c.ok == 2);
    CHECK(c.bad == 1);
}

TEST(test_overlong_line_is_bad_not_overflow) {
    link_check_t c;
    link_check_init(&c);
    char big[200];
    memset(big, 'x', sizeof big - 2);
    big[sizeof big - 2] = '\n';
    big[sizeof big - 1] = 0;
    feed(&c, big);
    feed(&c, "ping 0\n");
    CHECK(c.bad == 1);
    CHECK(c.ok == 1);
}

TEST(test_format_ping) {
    char buf[LINK_LINE_MAX];
    int n = link_format_ping(buf, sizeof buf, 42);
    CHECK(n == 8);
    CHECK(strcmp(buf, "ping 42\n") == 0);
}

int main(void) {
    RUN(test_in_order_replies_count_ok);
    RUN(test_line_split_across_reads);
    RUN(test_gap_in_sequence_counts_lost);
    RUN(test_garbage_counts_bad_and_keeps_sequence);
    RUN(test_repeat_or_old_number_is_bad);
    RUN(test_overlong_line_is_bad_not_overflow);
    RUN(test_format_ping);
    TEST_MAIN_END();
}
