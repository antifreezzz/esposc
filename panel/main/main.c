#include "display.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "panel";

// Проверка экрана: круг в центре, точка под пальцем, координаты в лог.
void app_main(void)
{
    ESP_ERROR_CHECK(display_start());
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0c0c0c), 0);
    lv_obj_t *ring = lv_arc_create(scr);
    lv_obj_set_size(ring, 300, 300);
    lv_obj_center(ring);
    lv_arc_set_value(ring, 60);
    lv_obj_t *dot = lv_obj_create(scr);
    lv_obj_set_size(dot, 30, 30);
    lv_obj_set_style_radius(dot, 15, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xff8800), 0);
    lvgl_port_unlock();
    int was = 0;
    for (;;) {
        int x, y;
        int down = display_touch(&x, &y);
        if (down) {
            lvgl_port_lock(0);
            lv_obj_set_pos(dot, x - 15, y - 15);
            lvgl_port_unlock();
        }
        if (down != was) {
            ESP_LOGI(TAG, "touch %s %d %d", down ? "down" : "up", x, y);
            was = down;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
