/*
 * Signal Strength screen implementation – displays WiFi and ESP-NOW signal strength
 */

#include "signal_strength_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "signal_strength"

#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */
#define COLOR_GOOD      0x07E0  /* Green */
#define COLOR_WARN      0xFFE0  /* Yellow */

/* Forward declaration */
static void draw_signal_bars(uint16_t x, uint16_t y, int8_t rssi);

void signal_strength_screen_init(signal_strength_screen_t *screen) {
    if (!screen) return;
    
    screen->wifi_rssi = -70;        /* Placeholder: weak signal */
    screen->wifi_connected = 1;     /* Placeholder: connected */
    screen->espnow_rssi = -60;      /* Placeholder: moderate signal */
    screen->nearby_devices = 3;     /* Placeholder: 3 devices nearby */
    screen->frame_count = 0;
    
    ESP_LOGI(TAG, "Signal strength screen initialized");
}

void signal_strength_screen_draw(signal_strength_screen_t *scr) {
    uint16_t ACCENT = settings_get_accent_color();
    uint16_t TEXT   = settings_get_text_color();
    st7789_fill(COLOR_BG);
    
    st7789_draw_string(4, 10, "Signal Strength", ACCENT, COLOR_BG, 2);
    st7789_fill_rect(0, 35, 320, 1, ACCENT);
    
    st7789_draw_string(4, 45, "WiFi:", ACCENT, COLOR_BG, 1);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d dBm", scr->wifi_rssi);
    st7789_draw_string(100, 45, buf, TEXT, COLOR_BG, 1);
    
    st7789_draw_string(4, 58, "ESP-NOW:", ACCENT, COLOR_BG, 1);
    snprintf(buf, sizeof(buf), "%d dBm (%d)", scr->espnow_rssi, scr->nearby_devices);
    st7789_draw_string(100, 58, buf, TEXT, COLOR_BG, 1);
    
    st7789_draw_string(4, 75, "WiFi Signal:", TEXT, COLOR_BG, 1);
    draw_signal_bars(10, 88, scr->wifi_rssi);
    
    st7789_draw_string(4, 105, "ESP-NOW Signal:", TEXT, COLOR_BG, 1);
    draw_signal_bars(100, 118, scr->espnow_rssi);
    
    st7789_draw_string(4, 150, "Press any button to exit", ACCENT, COLOR_BG, 1);
    scr->frame_count++;
}

void signal_strength_screen_exit(void) {
    st7789_fill(0x0000);  /* Black */
    ESP_LOGI(TAG, "Exited signal strength screen");
}

/* Helper: draw signal strength bars */
static void draw_signal_bars(uint16_t x, uint16_t y, int8_t rssi) {
    /* Convert RSSI to 0-4 bar count: -100 dBm = 0 bars, -50 dBm = 4 bars */
    uint8_t bars = 0;
    if (rssi > -50) bars = 4;
    else if (rssi > -60) bars = 3;
    else if (rssi > -80) bars = 2;
    else if (rssi > -100) bars = 1;
    
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t color = (i < bars) ? COLOR_GOOD : COLOR_BG;
        uint16_t bar_height = 5 + i * 2;  /* Progressively taller bars */
        st7789_fill_rect(x + i * 8, y + (10 - bar_height), 6, bar_height, color);
    }
}
