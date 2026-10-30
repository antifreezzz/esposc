# Ядро синта, чистый C. Хост-сборка (host/) берёт исходники напрямую и этот файл не читает.
idf_component_register(SRCS "acid_osc.c" "acid_env.c" "acid_filter.c" "acid_seq.c"
                            "acid_pattern.c" "acid_synth.c" "acid_pcm.c" "acid_kick.c"
                            "acid_noise.c" "acid_drums.c" "acid_arr.c" "acid_midi.c" "acid_fx.c" "acid_clock.c" "acid_tonal.c" "acid_mem.c" "acid_demo.c"
                       INCLUDE_DIRS "include"
                       LDFRAGMENTS "linker.lf"
                       REQUIRES plaits
                       PRIV_REQUIRES heap)
