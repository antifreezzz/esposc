#pragma once
#include "acid_link.h"
#include "esp_err.h"
#include "pl_proto.h"

// Связь с панелью Guition по UART1 (IO17 TX, IO18 RX, 1 Мбод): кадры pl_proto, своя задача на ядре 0.
// Из рендера - только очереди без ожидания.

// Синт и связку задача читает без лока для снимка: устаревшая цифра на экране до следующего события, звук не трогает.
// demo_on - режим для снимка (1 демо).
esp_err_t panel_uart_start(const acid_synth_t *s, const acid_link_t *l, const volatile int *demo_on);
// Правка с панели: 1 если есть (без ожидания).
int panel_uart_receive(pl_set_t *out);
// Событие для панели (без ожидания; очередь полна - теряется, следующий снимок поправит).
void panel_uart_send_knob(const acid_link_ev_t *ev);
// Отправить панели снимок при первой возможности.
void panel_uart_snapshot_due(void);
