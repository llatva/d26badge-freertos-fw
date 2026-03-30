/*
 * Sensor Readout screen implementation – displays temperature, humidity, and other sensor data
 */

#include "sensor_readout_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "sensor_readout"

/* Colors */
#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */

/* State tracking to avoid full-screen redraws */
static bool  s_needs_full_redraw = true;
static float s_last_temperature  = -999.0f;
static float s_last_humidity     = -999.0f;

void sensor_readout_screen_init(sensor_readout_screen_t *screen) {
    if (!screen) return;
    
    screen->temperature = 25.5f;  /* Placeholder value */
    screen->humidity = 45.0f;     /* Placeholder value */
    screen->frame_count = 0;
    
    /* Force a full redraw on next draw call */
    s_needs_full_redraw = true;
    s_last_temperature  = -999.0f;
    s_last_humidity     = -999.0f;
    
    ESP_LOGI(TAG, "Sensor readout screen initialized");
}

void sensor_readout_screen_draw(sensor_readout_screen_t *scr) {
    uint16_t ACCENT = settings_get_accent_color();
    uint16_t TEXT   = settings_get_text_color();

    /* Draw static elements only once */
    if (s_needs_full_redraw) {
        st7789_fill(COLOR_BG);
        st7789_draw_string(4, 10, "Sensor Readout", ACCENT, COLOR_BG, 2);
        st7789_fill_rect(0, 35, 320, 1, ACCENT);
        st7789_draw_string(4, 150, "Press any button to exit", ACCENT, COLOR_BG, 1);
        s_needs_full_redraw = false;
    }

    /* Redraw temperature only when the value changes */
    if (scr->temperature != s_last_temperature) {
        char temp_str[32];
        snprintf(temp_str, sizeof(temp_str), "Temperature: %-6.1f C", scr->temperature);
        st7789_draw_string(4, 45, temp_str, TEXT, COLOR_BG, 1);
        s_last_temperature = scr->temperature;
    }

    /* Redraw humidity only when the value changes */
    if (scr->humidity != s_last_humidity) {
        char humid_str[32];
        snprintf(humid_str, sizeof(humid_str), "Humidity: %-6.1f %%", scr->humidity);
        st7789_draw_string(4, 58, humid_str, TEXT, COLOR_BG, 1);
        s_last_humidity = scr->humidity;
    }

    /* Frame counter always changes – use padded format to overwrite old digits */
    char status_str[32];
    snprintf(status_str, sizeof(status_str), "Frame: %-10lu", scr->frame_count);
    st7789_draw_string(4, 75, status_str, TEXT, COLOR_BG, 1);
    
    scr->frame_count++;
}

void sensor_readout_screen_exit(void) {
    st7789_fill(0x0000);  /* Black */
    ESP_LOGI(TAG, "Exited sensor readout screen");
}
