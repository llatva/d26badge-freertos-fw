#include "color_select_screen.h"
#include "st7789.h"
#include "buttons.h"
#include "badge_settings.h"
#include <string.h>

#define GRID_W  6
#define GRID_H  2
#define COLOR_MAX_OPTIONS (GRID_W * GRID_H)

/* Palette: 12 distinct colours (RGB888 → RGB565)
 * White       #FFFFFF   Orange     #FF8000   Lime       #00FF00
 * Red         #FF0000   Yellow     #FFFF00   Teal       #00C8A0
 * Cyan        #00DCFF   Blue       #0050FF   Purple     #8000FF
 * Magenta     #FF00A0   Pink       #FF6496   Warm White #FFC896
 */
static const uint16_t s_colors[COLOR_MAX_OPTIONS] = {
    /* Row 1: White, Red, Orange, Yellow, Lime, Teal */
    0xFFFF, 0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x0654,
    /* Row 2: Cyan, Blue, Purple, Magenta, Pink, Warm-White */
    0x06FF, 0x029F, 0x801F, 0xF814, 0xFB32, 0xFE52,
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
    scr->confirmed = false;
    scr->cancelled = false;

    for (int i = 0; i < COLOR_MAX_OPTIONS; i++) {
        if (s_colors[i] == current) {
            scr->selected_idx = i;
            break;
        }
    }
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
    
    st7789_draw_string(4, 155, "Arrows:Nav  A:Save  B:Back", TEXT == 0x0000 ? 0x8410 : TEXT, bg, 1);
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
            scr->confirmed = true;
            return;  /* don't update position */
        case BTN_B:
            scr->cancelled = true;
            return;  /* don't update position */
        default:
            return;
    }
    scr->selected_idx = r * GRID_W + c;
}

uint16_t color_select_screen_get_color(color_select_screen_t *scr) {
    return s_colors[scr->selected_idx];
}

bool color_select_screen_is_confirmed(color_select_screen_t *scr) {
    return scr->confirmed;
}

bool color_select_screen_is_cancelled(color_select_screen_t *scr) {
    return scr->cancelled;
}
