# Прошивка панели Guition 4848S040: экран интерфейса синта. Общий код с синтом - components/panel_link.
cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../components/panel_link)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esposc_panel)
