/*
 * WLAN Spectrum Analyzer screen – displays WiFi channels and signal strength
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Maximum number of WiFi channels to display */
#define MAX_WIFI_CHANNELS 14

/* Screen for displaying WiFi channel spectrum */
typedef struct {
    uint8_t channel_rssi[MAX_WIFI_CHANNELS];  /* Signal strength per channel (0-100) */
    uint32_t frame_count;                      /* Frames rendered */
    uint8_t num_channels;                      /* Number of channels to display */
    uint8_t selected_channel;                  /* Currently selected channel for details */
} wlan_spectrum_screen_t;

/**
 * @brief  Initialise the WLAN spectrum analyzer screen.
 */
void wlan_spectrum_screen_init(wlan_spectrum_screen_t *screen);

/**
 * @brief  Render WLAN spectrum to display.
 */
void wlan_spectrum_screen_draw(wlan_spectrum_screen_t *screen);

/**
 * @brief  Handle button input for channel selection.
 */
void wlan_spectrum_screen_handle_button(wlan_spectrum_screen_t *screen, int button_id);

/**
 * @brief  Update channel data (simulated or from actual WiFi scan).
 */
void wlan_spectrum_screen_update_channels(wlan_spectrum_screen_t *screen);

/**
 * @brief  Exit WLAN spectrum screen and return to menu.
 */
void wlan_spectrum_screen_exit(void);
