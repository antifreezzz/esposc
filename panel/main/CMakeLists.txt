idf_component_register(SRCS "main.c" "display.c" "fonts/bc_15.c" "fonts/bc_17.c" "fonts/bc_22.c" "fonts/bc_32.c" "fonts/bc_58.c" "link.c" "ui.c" "ui_custom.c" "ui_mod.c" "ui_grids.c" "ui_seq.c" "ui_perf.c" "petri_bench.c" "petri_task.c" "ui_petri.c" INCLUDE_DIRS "." REQUIRES panel_link petri esp_driver_uart esp_driver_i2c esp_driver_ledc esp_lcd esp_timer esp_driver_gpio)
# Стенд чашки Петри: idf.py -C panel -B panel/build-bench -DPETRI_BENCH=1 ... (свой каталог сборки).
if(PETRI_BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PETRI_BENCH)
endif()
