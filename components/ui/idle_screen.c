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
    
    st7789_draw_string(268, 2, "WLAN", wlan_color, COLOR_BG, 1);
    st7789_draw_string(290, 12, "BT", bt_color, COLOR_BG, 1);

    /* Draw decorative top line */
    st7789_fill_rect(0, 20, 320, 1, ACCENT);

    /* Draw nickname centered, larger font (scale 4) */
    /* Scale 4: each char is 8 pixels base × 4 scale = 32 pixels wide, 16x4=64 pixels tall */
    const char *display_name = (nickname && nickname[0] != '\0') ? nickname : "badge";
    int nickname_len = strlen(display_name);
    int char_width = 8 * 4;  /* 32 pixels wide */
    int total_width = nickname_len * char_width;
    int start_x = (320 - total_width) / 2;
    if (start_x < 0) start_x = 0;
    
    st7789_draw_string(start_x, 45, (char *)display_name, COLOR_TEXT, COLOR_BG, 4);

    /* Draw decorative bottom line */
    st7789_fill_rect(0, 115, 320, 1, ACCENT);

    /* Draw version/status at bottom */
    st7789_draw_string(4, 150, "v0.5.1 | Press any button to enter menu", ACCENT, COLOR_BG, 1);
}

void idle_screen_clear(void) {
    st7789_fill(0x0000);  /* Black */
}

void idle_screen_reset(void) {
    /* Reset state so next draw does a full redraw */
    s_needs_full_redraw = true;
    s_last_time_str[0] = '\0';
}
