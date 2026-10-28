# Plaits (Mutable Instruments, MIT) за C-обёрткой. Хост-сборка (host/) этот файл не читает.
file(GLOB_RECURSE PLAITS_SRCS ${CMAKE_CURRENT_LIST_DIR}/eurorack/*.cc)
idf_component_register(SRCS "acid_plaits.cc" ${PLAITS_SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "eurorack")
# TEST - переносимый режим Plaits (без ассемблера ARM), stdio нужен его user_data.h.
target_compile_definitions(${COMPONENT_LIB} PRIVATE TEST)
target_compile_options(${COMPONENT_LIB} PRIVATE -include stdio.h -w)
