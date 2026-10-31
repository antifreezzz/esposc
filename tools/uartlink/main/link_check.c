#include "link_check.h"

#include <stdio.h>
#include <string.h>

void link_check_init(link_check_t *c) { memset(c, 0, sizeof *c); }

// Номер из "ping N", -1 если строка не такая.
static int64_t parse_ping(const char *s, int len) {
    if (len < 6 || memcmp(s, "ping ", 5) != 0) return -1;
    int64_t n = 0;
    for (int i = 5; i < len; i++) {
        if (s[i] < '0' || s[i] > '9' || n > 0xffffffffLL / 10) return -1;
        n = n * 10 + (s[i] - '0');
    }
    return n;
}

static void on_line(link_check_t *c) {
    int64_t n = c->len < 0 ? -1 : parse_ping(c->line, c->len);
    if (n < 0) {
        c->bad++;
        c->garbled++;
    } else if ((uint32_t)n < c->next) {
        c->bad++; // повтор или старый номер
    } else {
        uint32_t gap = (uint32_t)n - c->next;
        c->lost += gap > c->garbled ? gap - c->garbled : 0;
        c->garbled = 0;
        c->next = (uint32_t)n + 1;
        c->ok++;
    }
    c->len = 0;
}

void link_check_byte(link_check_t *c, unsigned char b) {
    if (b == '\n') {
        on_line(c);
    } else if (c->len >= 0) {
        if (c->len < LINK_LINE_MAX) c->line[c->len++] = (char)b;
        else c->len = -1;
    }
}

int link_format_ping(char *buf, size_t size, uint32_t n) {
    return snprintf(buf, size, "ping %lu\n", (unsigned long)n);
}
