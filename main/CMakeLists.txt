idf_component_register(SRCS "main.c" "usb_host_task.c" "audio_out_uac.c" "midi_in_usb.c"
                       INCLUDE_DIRS "."
                       REQUIRES acid esp_timer)
