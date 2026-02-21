idf_component_register(
    SRCS
        "micropython_runner.c"
        "mp_bridge.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        esp_system
        esp_common
        nvs_flash
)

# TODO: Add MicroPython library integration
# This will be expanded when we integrate the actual MicroPython VM
