#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int selected_idx;
    bool confirmed;
    const char *title;
} color_select_screen_t;

void color_select_screen_init(color_select_screen_t *scr, uint16_t current, const char *title);
void color_select_screen_draw(color_select_screen_t *scr);
void color_select_screen_handle_button(color_select_screen_t *scr, int btn_id);
uint16_t color_select_screen_get_color(color_select_screen_t *scr);
bool color_select_screen_is_confirmed(color_select_screen_t *scr);
