/*
 * Signal Strength screen – displays WiFi/ESP-NOW signal strength
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Screen for displaying signal strength */
typedef struct {
    int8_t wifi_rssi;           /* WiFi signal strength in dBm */
    uint8_t wifi_connected;     /* WiFi connection status */
    int8_t espnow_rssi;         /* ESP-NOW signal strength in dBm */
    uint8_t nearby_devices;     /* Number of nearby devices */
    uint32_t frame_count;       /* Frames rendered */
} signal_strength_screen_t;

/**
 * @brief  Initialise the signal strength screen.
 */
void signal_strength_screen_init(signal_strength_screen_t *screen);

/**
 * @brief  Render signal strength data to display.
 */
void signal_strength_screen_draw(signal_strength_screen_t *screen);

/**
 * @brief  Exit signal strength screen and return to menu.
 *         Clears display.
 */
void signal_strength_screen_exit(void);
