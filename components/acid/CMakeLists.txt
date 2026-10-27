# Ядро синта, чистый C. Хост-сборка (host/) берёт исходники напрямую и этот файл не читает.
file(GLOB srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
