#pragma once
#include "acid_link.h"
#include "acid_scene.h"
#include "esp_err.h"
#include "pl_load.h"
#include "pl_proto.h"

// Связь с панелью Guition по UART1 (IO17 TX, IO18 RX, 1 Мбод): кадры pl_proto, своя задача на ядре 0.
// Из рендера - только очереди без ожидания.

// Синт и связку задача читает без лока для снимка: устаревшая цифра на экране до следующего события, звук не трогает.
// demo_on - режим для снимка (1 демо), demo_song - номер песни демо (PL_DEMO_*).
// Свои страницы и сцены (c, sc) - так же для кадров PL_CUSTOM и PL_SCENE_STATE.
esp_err_t panel_uart_start(const acid_synth_t *s, const acid_link_t *l, const acid_custom_t *c, const acid_scene_t *sc,
                           const volatile int *demo_on, const volatile int *demo_song);
// Панель попросила следующую песню демо: 1 один раз на просьбу (без ожидания).
int panel_uart_take_demo(void);
// Правка с панели: 1 если есть (без ожидания).
int panel_uart_receive(pl_set_t *out);
// На панели открыли страницу: 1 если есть (без ожидания).
int panel_uart_receive_page(int *page);
// Событие для панели (без ожидания; очередь полна - теряется, следующий снимок поправит).
void panel_uart_send_knob(const acid_link_ev_t *ev);
// Отправить панели снимок при первой возможности.
void panel_uart_snapshot_due(void);
// Правка цели своей страницы с панели: 1 если есть (без ожидания).
int panel_uart_receive_edit(pl_custom_edit_t *out);
// Вызов или запись сцены с панели: 1 если есть (без ожидания).
int panel_uart_receive_scene(pl_scene_t *out);
// Отправить кадры PL_CUSTOM своих страниц из маски (бит up) при первой возможности.
void panel_uart_custom_due(unsigned mask);
// Отправить состояние сцен при первой возможности.
void panel_uart_scene_due(void);
// Таблица чашки Петри с панели (последняя, без ожидания): 1 если пришла новая.
int panel_uart_receive_petri(acid_petri_frame_t *out);
// Собранных таблиц чашки с запуска (для лога).
uint32_t panel_uart_petri_count(void);
// Загрузка ядер для панели: последнее значение уходит при следующем обороте задачи связи (новое затирает).
void panel_uart_send_load(const pl_load_t *l);
