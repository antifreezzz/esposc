#include "display.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

// Пины и тайминги Guition ESP32-4848S040 (проверены в minimaxdumbtest).
#define LCD_W 480
#define LCD_H 480
#define PCLK_HZ (12 * 1000 * 1000)
#define PIN_DE 18
#define PIN_VSYNC 17
#define PIN_HSYNC 16
#define PIN_PCLK 21
#define PIN_SPI_CS 39
#define PIN_SPI_SCK 48
#define PIN_SPI_SDA 47
#define PIN_BL 38
#define PIN_TP_SDA 19
#define PIN_TP_SCL 45
#define DRAW_LINES 48   // буфер отрисовки LVGL во внутренней памяти
// Строки буфера во внутренней памяти: кадр из PSRAM идёт на экран через него, прерывание дозаполняет его раз в
// BOUNCE_LINES строк (строка 46 мкс). При 10 строках опоздание прерывания на ~0.4 мс уже показывало старые полосы
// (на кадр - несколько копий картинки со сдвигом); 20 строк дают вдвое больший запас, цена - ещё 19 КБ внутренней RAM.
#define BOUNCE_LINES 20

static const char *TAG = "display";
static esp_lcd_touch_handle_t s_touch;
static int64_t s_refr_t0, s_refr_us, s_refr_max;
static int s_frames;

// Замер: сколько кадров LVGL перерисовал и сколько это стоило.
static void refr_cb(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        s_refr_t0 = now;
    } else {
        int64_t dt = now - s_refr_t0;
        s_refr_us += dt;
        s_refr_max = dt > s_refr_max ? dt : s_refr_max;
        s_frames++;
    }
}

void display_stats(int *frames, int *mean_us, int *max_us)
{
    *frames = s_frames;
    *mean_us = s_frames ? (int)(s_refr_us / s_frames) : 0;
    *max_us = (int)s_refr_max;
    s_frames = 0;
    s_refr_us = s_refr_max = 0;
}

// Инициализация ST7701 дословно из reference-прошивки панели.
static const st7701_lcd_init_cmd_t INIT[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x31, 0x05}, 2, 0},
    {0xCD, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x60}, 1, 0},
    {0xB1, (uint8_t[]){0x32}, 1, 0},
    {0xB2, (uint8_t[]){0x07}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x49}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t[]){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t[]){0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t[]){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE5, (uint8_t[]){0xE4}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x3A, (uint8_t[]){0x60}, 1, 10}, // RGB666, вход RGB565 совместим
    {0x11, NULL, 0, 120},             // sleep out
    {0x29, NULL, 0, 0},               // display on
};

static void backlight(int duty)
{
    ledc_timer_config_t t = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                             .timer_num = LEDC_TIMER_0, .freq_hz = 600, .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&t);
    ledc_channel_config_t c = {.gpio_num = PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
                               .timer_sel = LEDC_TIMER_0, .duty = duty};
    ledc_channel_config(&c);
}

static esp_err_t touch_start(void)
{
    i2c_master_bus_config_t bus_cfg = {.i2c_port = I2C_NUM_0, .sda_io_num = PIN_TP_SDA, .scl_io_num = PIN_TP_SCL,
                                       .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                       .flags.enable_internal_pullup = 1};
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c");
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "touch io");
    // Координаты GT911 уже в ориентации экрана (проверено в reference).
    esp_lcd_touch_config_t cfg = {.x_max = LCD_W, .y_max = LCD_H, .rst_gpio_num = GPIO_NUM_NC,
                                  .int_gpio_num = GPIO_NUM_NC};
    return esp_lcd_touch_new_i2c_gt911(io, &cfg, &s_touch);
}

int display_touch(int *x, int *y)
{
    if (!s_touch || esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        return 0;
    }
    esp_lcd_touch_point_data_t p[1];
    uint8_t n = 0;
    if (esp_lcd_touch_get_data(s_touch, p, &n, 1) != ESP_OK || n == 0) {
        return 0;
    }
    *x = p[0].x;
    *y = p[0].y;
    return 1;
}

esp_err_t display_start(void)
{
    backlight(0);
    spi_line_config_t line = {.cs_io_type = IO_TYPE_GPIO, .cs_gpio_num = PIN_SPI_CS,
                              .scl_io_type = IO_TYPE_GPIO, .scl_gpio_num = PIN_SPI_SCK,
                              .sda_io_type = IO_TYPE_GPIO, .sda_gpio_num = PIN_SPI_SDA,
                              .io_expander = NULL};
    esp_lcd_panel_io_3wire_spi_config_t io_cfg = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line, 0);
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(&io_cfg, &io), TAG, "3wire spi");

    esp_lcd_rgb_panel_config_t rgb = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {.pclk_hz = PCLK_HZ, .h_res = LCD_W, .v_res = LCD_H,
                    .hsync_pulse_width = 8, .hsync_back_porch = 50, .hsync_front_porch = 10,
                    .vsync_pulse_width = 8, .vsync_back_porch = 20, .vsync_front_porch = 10},
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .bounce_buffer_size_px = LCD_W * BOUNCE_LINES,
        .hsync_gpio_num = PIN_HSYNC,
        .vsync_gpio_num = PIN_VSYNC,
        .de_gpio_num = PIN_DE,
        .pclk_gpio_num = PIN_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        // D0..D15 = B0..B4, G0..G5, R0..R4
        .data_gpio_nums = {4, 5, 6, 7, 15, 8, 20, 3, 46, 9, 10, 11, 12, 13, 14, 0},
        .flags.fb_in_psram = 1,
    };
    st7701_vendor_config_t vendor = {.init_cmds = INIT, .init_cmds_size = sizeof INIT / sizeof INIT[0],
                                     .rgb_config = &rgb, .flags = {.mirror_by_cmd = 1, .auto_del_panel_io = 0}};
    esp_lcd_panel_dev_config_t dev = {.reset_gpio_num = GPIO_NUM_NC, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                      .bits_per_pixel = 16, .vendor_config = &vendor};
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(io, &dev, &panel), TAG, "st7701");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; // ядро 0 - связь и тач
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "lvgl port");
    lvgl_port_display_cfg_t disp = {
        // Рисуем кусками во внутренней памяти: программная отрисовка прямо в кадр в PSRAM стоила ~100 мс на экран.
        // Буфер один: выдача куска синхронная (копия в кадр и сразу готово), второй буфер LVGL не пригодился бы,
        // а 46 КБ внутренней памяти нужны буферу экрана и чашке Петри.
        .io_handle = io, .panel_handle = panel, .buffer_size = LCD_W * DRAW_LINES, .double_buffer = 0,
        .hres = LCD_W, .vres = LCD_H, .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_dma = 1},
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {.bb_mode = 1}};
    lv_display_t *d = lvgl_port_add_disp_rgb(&disp, &rgb_cfg);
    if (!d) {
        return ESP_FAIL;
    }
    lvgl_port_lock(0);
    lv_display_add_event_cb(d, refr_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(d, refr_cb, LV_EVENT_REFR_READY, NULL);
    lvgl_port_unlock();
    ESP_RETURN_ON_ERROR(touch_start(), TAG, "touch");
    backlight(200);
    return ESP_OK;
}
