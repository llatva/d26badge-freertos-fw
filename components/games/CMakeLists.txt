idf_component_register(
    SRCS "hacky_bird.c" "space_shooter.c" "snake.c"
    INCLUDE_DIRS "include"
    REQUIRES st7789 buttons freertos ui sk6812
)
