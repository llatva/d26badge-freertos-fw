/*
 * WLAN Networks List screen – shows detected WiFi networks
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_wifi.h"

/* Maximum number of WiFi networks to track */
#define MAX_WLAN_NETWORKS 50

/* WiFi network information */
typedef struct {
    char ssid[33];           /* Network SSID */
    int8_t rssi;             /* Signal strength in dBm */
    uint8_t channel;         /* WiFi channel */
    wifi_auth_mode_t auth;   /* Authentication mode */
} wlan_network_info_t;

/* Screen for displaying WiFi networks list */
typedef struct {
    wlan_network_info_t networks[MAX_WLAN_NETWORKS];  /* Detected networks */
    uint16_t num_networks;                             /* Number of detected networks */
    uint8_t scroll_offset;                             /* Scroll position */
} wlan_list_screen_t;

/**
 * @brief  Initialise the WLAN networks list screen.
 */
void wlan_list_screen_init(wlan_list_screen_t *screen);

/**
 * @brief  Start WiFi scanning task.
 */
void wlan_list_screen_start_scan(wlan_list_screen_t *screen);

/**
 * @brief  Stop WiFi scanning task.
 */
void wlan_list_screen_stop_scan(void);

/**
 * @brief  Render WLAN networks list to display.
 */
void wlan_list_screen_draw(wlan_list_screen_t *screen);

/**
 * @brief  Handle button input for scrolling.
 */
void wlan_list_screen_handle_button(wlan_list_screen_t *screen, int button_id);

/**
 * @brief  Exit WLAN networks list screen and return to menu.
 */
void wlan_list_screen_exit(void);
