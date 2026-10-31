// Сверка эха: синт шлёт "ping N\n", панель возвращает байты как есть.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define LINK_LINE_MAX 32

typedef struct {
    char line[LINK_LINE_MAX];
    int len;          // -1: строка переполнилась, ждём '\n'
    uint32_t next;    // какой номер ждём
    uint32_t garbled; // испорченных строк с последнего верного ответа: они занимают место в пропуске номеров
    uint32_t ok, bad, lost;
} link_check_t;

void link_check_init(link_check_t *c);
void link_check_byte(link_check_t *c, unsigned char b);
int link_format_ping(char *buf, size_t size, uint32_t n);
