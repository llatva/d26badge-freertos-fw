idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES st7789 sk6812 buttons menu_ui audio ui freertos esp_common games pyapps_fs micropython_runner
             nvs_flash esp_wifi esp_netif esp_event
)
