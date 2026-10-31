idf_component_register(SRCS "main.c" "link_check.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_uart esp_driver_gpio esp_timer)
# Роль: idf.py -B build-synth -DLINK_ROLE=synth ... или -B build-panel -DLINK_ROLE=panel
if(LINK_ROLE STREQUAL "panel")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_PANEL=1)
elseif(LINK_ROLE STREQUAL "synth")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_PANEL=0)
else()
    message(FATAL_ERROR "LINK_ROLE must be synth or panel")
endif()
if(LINK_BAUD)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_BAUD=${LINK_BAUD})
endif()
if(LINK_DIAG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_DIAG=${LINK_DIAG})
endif()
