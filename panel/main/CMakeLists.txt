idf_component_register(SRCS "main.c" "display.c" "link.c" "ui.c" INCLUDE_DIRS "." REQUIRES panel_link esp_driver_uart esp_driver_i2c esp_driver_ledc esp_lcd esp_timer esp_driver_gpio)
