/*
 * About screen implementation
 */

#include "about_screen.h"
#include "st7789.h"
#include "badge_settings.h"

#define COLOR_BG        0x0000  /* Black */

void about_screen_draw(void) {
    uint16_t ACCENT = settings_get_accent_color();
    uint16_t TEXT   = settings_get_text_color();
    
    st7789_fill(COLOR_BG);
    
    /* Header */
    st7789_draw_string(4, 10, "About", ACCENT, COLOR_BG, 2);
    
    /* Decoration line */
    st7789_fill_rect(0, 35, 320, 1, ACCENT);
    
    /* Body */
    st7789_draw_string(4, 45, "Disobey Badge 2025/26", TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 60, "FW: v0.6.1 (FreeRTOS) by hzb", TEXT, COLOR_BG, 1);
    
    st7789_draw_string(4, 75, "Hardware:", ACCENT, COLOR_BG, 1);
    st7789_draw_string(4, 88, "- ESP32-S3 (WROOM-1-N16R8)", TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 100, "- ST7789 320x170 LCD", TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 112, "- 8x SK6812 NeoPixels", TEXT, COLOR_BG, 1);

    st7789_draw_string(4, 125, "Features:", ACCENT, COLOR_BG, 1);
    st7789_draw_string(4, 138, "- Multithreaded FreeRTOS", TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 148, "- MicroPython integration", TEXT, COLOR_BG, 1);

    st7789_draw_string(4, 158, "Press any button to continue", ACCENT, COLOR_BG, 1);
}

void about_screen_clear(void) {
    st7789_fill(0x0000);
}
