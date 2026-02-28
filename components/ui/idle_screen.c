/*
 * Idle screen implementation – displays nickname prominently
 */

#include "idle_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

/* Colors */
#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */
#define COLOR_TIME      0xB7E0  /* Light gray for time/date */
#define COLOR_ENABLED   0x07E0  /* Green for enabled status */
#define COLOR_DISABLED  0x2945  /* Dark gray for disabled status */

/* Static variables to track if we need to redraw */
static char s_last_time_str[16] = "";
static bool s_needs_full_redraw = true;

void idle_screen_draw(const char *nickname) {
    uint16_t ACCENT = settings_get_accent_color();

    /* Time display */
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    
    /* Format compact date and time (e.g., "Feb 21 14:32") */
    char datetime_str[16];
    strftime(datetime_str, sizeof(datetime_str), "%b %d %H:%M", timeinfo);

    /* Check if time has changed */
    bool time_changed = (strcmp(datetime_str, s_last_time_str) != 0);
    
    if (!s_needs_full_redraw && !time_changed) {
        /* Time hasn't changed and we've already drawn - skip redraw */
        return;
    }

    /* Time changed or first draw - do a full screen update */
    strncpy(s_last_time_str, datetime_str, sizeof(s_last_time_str) - 1);
    s_needs_full_redraw = false;

    /* Clear screen completely */
    st7789_fill(COLOR_BG);

    /* Draw date/time at top left in small font (scale 1: 8px per char) */
    st7789_draw_string(4, 2, datetime_str, COLOR_TIME, COLOR_BG, 1);

    /* Draw WLAN and BT status at top right */
    uint16_t wlan_color = COLOR_ENABLED;
    uint16_t bt_color = COLOR_ENABLED;
    
    st7789_draw_string(240, 2, "WLAN", wlan_color, COLOR_BG, 1);
    st7789_draw_string(290, 2, "BT", bt_color, COLOR_BG, 1);

    /* Draw decorative top line */
    st7789_fill_rect(0, 20, 320, 1, ACCENT);

    /* Draw nickname centered – dynamic scale to best-fit the screen.
     * Screen: 320px wide.  Nickname area: y 21..170 = 149px tall.
     * Base font: 8px wide × 16px tall.  At scale S: 8S × 16S.
     * Pick the largest integer scale that fits both width and height.
     * Max scale is 6: the st7789 char_buf is 48×96 = 4608 pixels (scale 6). */
    const char *display_name = (nickname && nickname[0] != '\0') ? nickname : "badge";
    int nickname_len = strlen(display_name);

    /* Largest scale that fits width:  8 * scale * len <= 320  →  scale <= 40/len */
    int scale_w = 40 / nickname_len;
    /* Largest scale that fits height: 16 * scale <= 149  →  scale <= 9
     * Cap at 6 – the driver's internal char buffer supports up to scale 6 */
    int scale = scale_w;
    if (scale > 6) scale = 6;
    if (scale < 1) scale = 1;

    int char_width  = 8 * scale;
    int char_height = 16 * scale;
    int total_width = nickname_len * char_width;

    /* Centre horizontally */
    int start_x = (320 - total_width) / 2;
    if (start_x < 0) start_x = 0;
    /* Centre vertically in the area below the top line (y=21) to bottom (y=170) */
    int area_h = 170 - 21;
    int start_y = 21 + (area_h - char_height) / 2;
    if (start_y < 21) start_y = 21;

    uint16_t TEXT = settings_get_text_color();
    st7789_draw_string(start_x, start_y, (char *)display_name, TEXT, COLOR_BG, scale);

    /* Draw decorative bottom line */
//    st7789_fill_rect(0, 115, 320, 1, ACCENT);
    /* Draw version/status at bottom */
//   st7789_draw_string(4, 150, "v0.5.1 | Press any button to enter menu", ACCENT, COLOR_BG, 1);
}

void idle_screen_clear(void) {
    st7789_fill(0x0000);  /* Black */
}

void idle_screen_reset(void) {
    /* Reset state so next draw does a full redraw */
    s_needs_full_redraw = true;
    s_last_time_str[0] = '\0';
}
