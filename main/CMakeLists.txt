# Вывод звука: ЦАП PCM5102A на I2S по умолчанию, USB-свисток (UAC) для опытов - idf.py -B build-uac -DESPOSC_UAC=1 ...
if(ESPOSC_UAC)
    set(AUDIO_OUT_SRC "audio_out_uac.c")
else()
    set(AUDIO_OUT_SRC "audio_out_i2s.c")
endif()
# Стенд цены голосов без звука: idf.py -B build-modelbench -DESPOSC_MODEL_BENCH=1 ... (main/model_bench.c).
if(ESPOSC_MODEL_BENCH)
    set(BENCH_SRC "model_bench.c")
endif()
idf_component_register(SRCS "main.c" ${BENCH_SRC} "usb_host_task.c" ${AUDIO_OUT_SRC} "midi_in_usb.c" "store.c" "samples_part.c" "panel_uart.c"
                       INCLUDE_DIRS "."
                       REQUIRES acid panel_link esp_timer nvs_flash esp_partition esp_driver_uart esp_driver_gpio
                                esp_driver_i2s)
# Стенд замеров: idf.py -B build-stress -DESPOSC_STRESS=1 ... (свой каталог сборки, чтобы не прошить случайно).
if(ESPOSC_MODEL_BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOSC_MODEL_BENCH)
endif()
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
# Проверка ЦАПа тоном вместо синта (сборка I2S): -DESPOSC_I2S_TONE=1, каталог build-i2s-tone.
if(ESPOSC_I2S_TONE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOSC_I2S_TONE)
endif()
