# Вывод звука: свисток UAC по умолчанию, ЦАП PCM5102A на I2S - idf.py -B build-i2s -DESPOSC_I2S=1 ...
if(ESPOSC_I2S)
    set(AUDIO_OUT_SRC "audio_out_i2s.c")
else()
    set(AUDIO_OUT_SRC "audio_out_uac.c")
endif()
idf_component_register(SRCS "main.c" "usb_host_task.c" ${AUDIO_OUT_SRC} "midi_in_usb.c" "store.c" "samples_part.c" "panel_uart.c"
                       INCLUDE_DIRS "."
                       REQUIRES acid panel_link esp_timer nvs_flash esp_partition esp_driver_uart esp_driver_gpio
                                esp_driver_i2s)
# Стенд замеров: idf.py -B build-stress -DESPOSC_STRESS=1 ... (свой каталог сборки, чтобы не прошить случайно).
if(ESPOSC_STRESS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOSC_STRESS)
endif()
# Стенд слоёв (вместе с ESPOSC_STRESS): -DESPOSC_STRESS_LAYERS=1, каталог build-stress-layers.
if(ESPOSC_STRESS_LAYERS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOSC_STRESS_LAYERS)
endif()
# Стенд PETRI (вместе с ESPOSC_STRESS): -DESPOSC_STRESS_PETRI=1, каталог build-stress-petri.
if(ESPOSC_STRESS_PETRI)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOSC_STRESS_PETRI)
endif()
