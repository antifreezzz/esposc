idf_component_register(SRCS "petri_sim.c" "petri_agents.c" "petri_capture.c" INCLUDE_DIRS "include")
# Панель собрана с -Og: на нём шаг сетки стоил 8.1 мс. Счёт чашки всегда с -O2.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
