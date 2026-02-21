idf_component_register(
    SRCS "pyapps_fs.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs wear_levelling esp_partition
)
