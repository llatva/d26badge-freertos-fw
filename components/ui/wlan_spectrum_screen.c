/*
 * WLAN Spectrum Analyzer implementation – displays WiFi channels and signal strength
 */

#include "wlan_spectrum_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "buttons.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#define TAG "wlan_spectrum"

#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */
#define COLOR_WEAK      0x4208  /* Dark gray */
#define COLOR_GOOD      0x07E0  /* Green */
#define COLOR_STRONG    0xFFE0  /* Yellow */

/* WiFi channel frequencies and bands */
static const char *channel_labels[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14"
};

void wlan_spectrum_screen_init(wlan_spectrum_screen_t *screen) {
    if (!screen) return;
    
    memset(screen, 0, sizeof(*screen));
    screen->num_channels = 13;  /* Support channels 1-13 (2.4 GHz) */
    screen->selected_channel = 6;
    
    /* Initialize with some test data */
    wlan_spectrum_screen_update_channels(screen);
    
    ESP_LOGI(TAG, "WLAN spectrum analyzer screen initialized");
}

void wlan_spectrum_screen_update_channels(wlan_spectrum_screen_t *screen) {
    if (!screen) return;
    
    /* TODO: In production, perform actual WiFi scan here
     * For now, use simulated data with some variation */
    for (uint8_t i = 0; i < screen->num_channels; i++) {
        /* Create a realistic spectrum pattern: peak around channel 6-7 */
        uint8_t distance = (i > 6) ? (i - 6) : (6 - i);
        uint8_t base_strength = 70 - (distance * 8);
        
        /* Add some variation */
        uint8_t variation = ((screen->frame_count + i * 7) % 20);
        if (variation > 10) variation = 20 - variation;
        
        screen->channel_rssi[i] = (base_strength + variation > 100) ? 100 : (base_strength + variation);
    }
}

void wlan_spectrum_screen_draw(wlan_spectrum_screen_t *scr) {
    uint16_t ACCENT = settings_get_accent_color();
    uint16_t TEXT   = settings_get_text_color();
    st7789_fill(COLOR_BG);
    
    st7789_draw_string(4, 10, "WiFi Spectrum", ACCENT, COLOR_BG, 2);
    st7789_fill_rect(0, 28, 320, 1, ACCENT);
    
    char details_str[48];
    snprintf(details_str, sizeof(details_str), "Ch %s: %d%% signal",
             channel_labels[scr->selected_channel],
             scr->channel_rssi[scr->selected_channel]);
    st7789_draw_string(4, 110, details_str, TEXT, COLOR_BG, 1);
    
    st7789_draw_string(4, 125, "LEFT/RIGHT: select channel", TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 138, "SELECT/A: scan | any button: exit", TEXT, COLOR_BG, 1);
    
    /* Draw spectrum bars for each channel */
    uint8_t channels_per_row = 13;
    uint16_t bar_width = 320 / channels_per_row;
    uint16_t bar_max_height = 60;
    uint16_t bar_start_y = 35;
    
    for (uint8_t i = 0; i < scr->num_channels; i++) {
        uint16_t x = (i * bar_width) + 1;
        uint8_t strength = scr->channel_rssi[i];
        uint16_t bar_height = (strength * bar_max_height) / 100;
        uint16_t y = bar_start_y + bar_max_height - bar_height;
        
        /* Color based on signal strength */
        uint16_t color;
        if (strength < 30) {
            color = COLOR_WEAK;
        } else if (strength < 60) {
            color = COLOR_GOOD;
        } else {
            color = COLOR_STRONG;
        }
        
        /* Highlight selected channel */
        if (i == scr->selected_channel) {
            st7789_fill_rect(x - 1, y - 2, bar_width - 2, bar_height + 4, COLOR_TEXT);
            st7789_fill_rect(x, y, bar_width - 2, bar_height, color);
        } else {
            st7789_fill_rect(x, y, bar_width - 2, bar_height, color);
        }
    }
    
    /* Draw channel numbers */
    for (uint8_t i = 0; i < scr->num_channels; i++) {
        uint16_t x = (i * bar_width) + (bar_width / 2) - 2;
        uint16_t y = bar_start_y + bar_max_height + 5;
        st7789_draw_string(x, y, (char *)channel_labels[i], COLOR_TEXT, COLOR_BG, 1);
    }
    
    scr->frame_count++;
}

void wlan_spectrum_screen_handle_button(wlan_spectrum_screen_t *screen, int button_id) {
    if (!screen) return;
    
    if (button_id == BTN_LEFT) {
        /* Previous channel */
        if (screen->selected_channel > 0) {
            screen->selected_channel--;
        } else {
            screen->selected_channel = screen->num_channels - 1;  /* Wrap around */
        }
    } else if (button_id == BTN_RIGHT) {
        /* Next channel */
        if (screen->selected_channel < screen->num_channels - 1) {
            screen->selected_channel++;
        } else {
            screen->selected_channel = 0;  /* Wrap around */
        }
    } else if (button_id == BTN_A || button_id == BTN_SELECT) {
        /* Perform WiFi scan (would be async in production) */
        ESP_LOGI(TAG, "WiFi scan requested on channel %s", 
                 channel_labels[screen->selected_channel]);
        wlan_spectrum_screen_update_channels(screen);
    }
}

void wlan_spectrum_screen_exit(void) {
    st7789_fill(0x0000);  /* Black */
    ESP_LOGI(TAG, "Exited WLAN spectrum screen");
}
