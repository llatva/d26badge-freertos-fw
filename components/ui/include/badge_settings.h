/*
 * Badge settings manager – persistent storage via NVS
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BADGE_NICKNAME_LEN 32

/* Settings structure */
typedef struct {
    char nickname[BADGE_NICKNAME_LEN];
    uint16_t accent_color;
    uint16_t text_color;
} badge_settings_t;

/**
 * @brief  Initialize settings system (load from NVS)
 */
void settings_init(void);

/**
 * @brief  Get current config values
 */
uint16_t settings_get_accent_color(void);
uint16_t settings_get_text_color(void);
const char *settings_get_nickname(void);

/**
 * @brief  Update setting values (saves to NVS)
 */
void settings_set_accent_color(uint16_t color);
void settings_set_text_color(uint16_t color);
void settings_set_nickname(const char *nickname);
const badge_settings_t *settings_get(void);
