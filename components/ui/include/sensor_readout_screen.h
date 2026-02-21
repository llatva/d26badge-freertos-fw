/*
 * Sensor Readout screen – displays data from badge sensors (temperature, humidity, etc.)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Screen for displaying sensor data */
typedef struct {
    float temperature;      /* Temperature in Celsius */
    float humidity;         /* Relative humidity in % */
    uint32_t frame_count;   /* Frames rendered */
} sensor_readout_screen_t;

/**
 * @brief  Initialise the sensor readout screen.
 */
void sensor_readout_screen_init(sensor_readout_screen_t *screen);

/**
 * @brief  Render sensor data to display.
 */
void sensor_readout_screen_draw(sensor_readout_screen_t *screen);

/**
 * @brief  Exit sensor readout screen and return to menu.
 *         Clears display.
 */
void sensor_readout_screen_exit(void);
