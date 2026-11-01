# Ядро синта, чистый C. Хост-сборка (host/) берёт исходники напрямую и этот файл не читает.
idf_component_register(SRCS "acid_osc.c" "acid_env.c" "acid_filter.c"
                            "acid_synth.c" "acid_pcm.c" "acid_kick.c"
                            "acid_noise.c" "acid_drums.c" "acid_midi.c" "acid_fx.c" "acid_clock.c" "acid_tonal.c" "acid_mem.c" "acid_demo.c" "acid_demo_song.c" "acid_stress_song.c" "acid_bank.c" "acid_cym.c" "acid_perc.c" "acid_drive.c" "acid_link.c" "acid_custom.c" "acid_scene.c"
                       INCLUDE_DIRS "include"
                       LDFRAGMENTS "linker.lf"
                       REQUIRES plaits panel_link
                       PRIV_REQUIRES heap)
# Стенд замеров: разбивка рендера по участкам (acid_prof.h).
if(ESPOSC_STRESS)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC ACID_PROFILE)
endif()
