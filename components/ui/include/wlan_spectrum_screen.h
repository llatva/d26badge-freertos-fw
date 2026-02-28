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
    int8_t channel_rssi[MAX_WIFI_CHANNELS];   /* Signal strength per channel in dBm */
    uint32_t frame_count;                      /* Frames rendered */
    uint8_t num_channels;                      /* Number of channels to display */
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
