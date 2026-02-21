idf_component_register(
    SRCS "menu_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES st7789 freertos
    PRIV_REQUIRES ui
)
