/*
 * WLAN Spectrum Analyzer screen – displays WiFi channels and signal strength
 *
 * WiFi must be initialised in STA mode before using this screen.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Maximum number of WiFi channels to display */
#define MAX_WIFI_CHANNELS 14

/* Screen state */
typedef struct {
    int8_t   channel_rssi[MAX_WIFI_CHANNELS];  /* best RSSI per channel (dBm) */
    uint32_t frame_count;
    uint8_t  num_channels;                     /* always 13 for 2.4 GHz */
    bool     needs_full_draw;
} wlan_spectrum_screen_t;

/**
 * @brief  Initialise the WLAN spectrum analyzer screen.
 */
void wlan_spectrum_screen_init(wlan_spectrum_screen_t *screen);

/**
 * @brief  Start WiFi channel scanning task.
 */
void wlan_spectrum_screen_start_scan(wlan_spectrum_screen_t *screen);

/**
 * @brief  Stop WiFi channel scanning task.
 */
void wlan_spectrum_screen_stop_scan(void);

/**
 * @brief  Render WLAN spectrum to display.
 */
void wlan_spectrum_screen_draw(wlan_spectrum_screen_t *screen);

/**
 * @brief  Exit WLAN spectrum screen and return to menu.
 */
void wlan_spectrum_screen_exit(void);
