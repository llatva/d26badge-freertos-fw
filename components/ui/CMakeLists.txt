idf_component_register(
    SRCS "text_input_screen.c" "badge_settings.c" "idle_screen.c" "about_screen.c" "ui_test_screen.c" "sensor_readout_screen.c" "signal_strength_screen.c" "wlan_spectrum_screen.c" "wlan_list_screen.c" "color_select_screen.c"
    INCLUDE_DIRS "include"
    REQUIRES "st7789" "buttons" "sk6812" "nvs_flash" "freertos" "esp_wifi"
)
