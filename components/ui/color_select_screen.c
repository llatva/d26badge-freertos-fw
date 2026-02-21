#include "color_select_screen.h"
#include "st7789.h"
#include "buttons.h"
#include "badge_settings.h"
#include <string.h>

#define GRID_W  6
#define GRID_H  2
#define COLOR_MAX_OPTIONS (GRID_W * GRID_H)

/* Palette: 12 vivid accent colours (RGB888 → RGB565)
 * #FF1A1A #FF6A00 #FFF200 #7CFC00 #00E600 #00FFD5
 * #00CFFF #004CFF #5E00FF #A000FF #FF00CC #FF4DA6
 */
static const uint16_t s_colors[COLOR_MAX_OPTIONS] = {
    /* Row 1: Red, Orange, Yellow, Lawn-Green, Green, Turquoise */
    0xF8C3, 0xFB40, 0xFFC4, 0x7FE0, 0x0720, 0x07FA,
    /* Row 2: Sky-Blue, Royal-Blue, Violet, Purple, Magenta, Hot-Pink */
    0x0677, 0x0260, 0x5800, 0xA007, 0xF819, 0xFA69,
};

#define BOX_W    42
#define BOX_H    50
#define GAP_X    6
#define GAP_Y    10
#define START_X  8
#define START_Y  45

void color_select_screen_init(color_select_screen_t *scr, uint16_t current, const char *title) {
    scr->selected_idx = 0;
    scr->title = title;
    
    for (int i = 0; i < COLOR_MAX_OPTIONS; i++) {
        if (s_colors[i] == current) {
            scr->selected_idx = i;
            break;
        }
    }
    scr->confirmed = false;
}

void color_select_screen_draw(color_select_screen_t *scr) {
    uint16_t bg = 0x0000;
    uint16_t TEXT = settings_get_text_color();
    st7789_fill(bg);
    
    st7789_draw_string(4, 10, scr->title, TEXT, bg, 2);
    st7789_fill_rect(0, 32, 320, 1, settings_get_accent_color());

    for (int i = 0; i < COLOR_MAX_OPTIONS; i++) {
        int r = i / GRID_W;
        int c = i % GRID_W;
        uint16_t x = START_X + c * (BOX_W + GAP_X);
        uint16_t y = START_Y + r * (BOX_H + GAP_Y);
        
        bool selected = (i == scr->selected_idx);
        
        if (selected) {
            /* Selection frame */
            st7789_fill_rect(x - 3, y - 3, BOX_W + 6, BOX_H + 6, TEXT);
        } else {
            /* Basic border */
            st7789_fill_rect(x - 1, y - 1, BOX_W + 2, BOX_H + 2, 0x4208);
        }
        
        st7789_fill_rect(x, y, BOX_W, BOX_H, s_colors[i]);
    }
    
    st7789_draw_string(4, 155, "Arrows: Nav, A: Save, B: Exit", TEXT == 0x0000 ? 0x8410 : TEXT, bg, 1);
}

void color_select_screen_handle_button(color_select_screen_t *scr, int btn_id) {
    int r = scr->selected_idx / GRID_W;
    int c = scr->selected_idx % GRID_W;

    switch (btn_id) {
        case BTN_UP:    r = (r == 0) ? GRID_H - 1 : r - 1; break;
        case BTN_DOWN:  r = (r + 1) % GRID_H; break;
        case BTN_LEFT:  c = (c == 0) ? GRID_W - 1 : c - 1; break;
        case BTN_RIGHT: c = (c + 1) % GRID_W; break;
        case BTN_A:
        case BTN_SELECT:
            scr->confirmed = true;
            break;
    }
    scr->selected_idx = r * GRID_W + c;
}

uint16_t color_select_screen_get_color(color_select_screen_t *scr) {
    return s_colors[scr->selected_idx];
}

bool color_select_screen_is_confirmed(color_select_screen_t *scr) {
    return scr->confirmed;
}
