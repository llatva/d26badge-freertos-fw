idf_component_register(
    SRCS "sk6812.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer
)
