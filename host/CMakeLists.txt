cmake_minimum_required(VERSION 3.16)
project(acid_host C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wall -Wextra -Werror -O2)

set(ACID_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/acid)
file(GLOB ACID_SRCS CONFIGURE_DEPENDS ${ACID_DIR}/*.c)
# Общий код панели и синта: протокол, подписи, жест, модель экрана.
set(PL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/panel_link)
file(GLOB PL_SRCS CONFIGURE_DEPENDS ${PL_DIR}/*.c)

# Plaits: чужой код, собираем без -Werror и со своими флагами.
set(PLAITS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/plaits)
file(GLOB_RECURSE PLAITS_SRCS CONFIGURE_DEPENDS ${PLAITS_DIR}/eurorack/*.cc)
add_library(plaits STATIC ${PLAITS_SRCS} ${PLAITS_DIR}/acid_plaits.cc)
target_include_directories(plaits PRIVATE ${PLAITS_DIR}/eurorack PUBLIC ${PLAITS_DIR}/include)
target_compile_definitions(plaits PRIVATE TEST)
target_compile_options(plaits PRIVATE -include stdio.h -Wno-error -w)

enable_testing()
file(GLOB TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${TEST_SRCS})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src} wav.c bank_fixture.c ${ACID_SRCS} ${PL_SRCS})
    target_include_directories(${name} PRIVATE ${ACID_DIR}/include ${PL_DIR}/include)
    target_link_libraries(${name} plaits m)
    add_test(NAME ${name} COMMAND ${name})
endforeach()

add_executable(acid_render render_wav.c wav.c bank_fixture.c ${ACID_SRCS})
target_include_directories(acid_render PRIVATE ${ACID_DIR}/include)
target_link_libraries(acid_render plaits m)
