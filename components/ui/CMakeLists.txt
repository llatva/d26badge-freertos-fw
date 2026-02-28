idf_component_register(
    SRCS "text_input_screen.c" "badge_settings.c" "idle_screen.c" "about_screen.c" "ui_test_screen.c" "sensor_readout_screen.c" "signal_strength_screen.c" "wlan_spectrum_screen.c" "wlan_list_screen.c" "color_select_screen.c" "sao_eeprom_screen.c"
    INCLUDE_DIRS "include"
    REQUIRES "st7789" "buttons" "sk6812" "nvs_flash" "freertos" "esp_wifi" "esp_netif" "esp_event" "esp_driver_i2c"
)

# version.h lives in menu_ui – add its include dir without a full component dependency
# (menu_ui already PRIV_REQUIRES ui, so a circular REQUIRES would fail)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/components/menu_ui/include")
