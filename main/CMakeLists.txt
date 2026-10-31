idf_component_register(SRCS "main.c" "usb_host_task.c" "audio_out_uac.c" "midi_in_usb.c" "store.c" "samples_part.c" "panel_uart.c"
                       INCLUDE_DIRS "."
                       REQUIRES acid panel_link esp_timer nvs_flash esp_partition esp_driver_uart)
# Стенд замеров: idf.py -B build-stress -DESPOSC_STRESS=1 ... (свой каталог сборки, чтобы не прошить случайно).
if(ESPOSC_STRESS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOSC_STRESS)
endif()
