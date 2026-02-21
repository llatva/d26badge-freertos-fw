idf_component_register(
    SRCS "audio.c" "audio_spectrum_screen.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver" "freertos" "esp_common" "st7789"
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
