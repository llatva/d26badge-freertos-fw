/*
 * Idle screen – displays nickname prominently
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  Draw idle screen with nickname centered on display
 * @param  nickname  The nickname to display
 */
void idle_screen_draw(const char *nickname);

/**
 * @brief  Reset idle screen state (call when entering idle state from another state)
 */
void idle_screen_reset(void);

/**
 * @brief  Clear idle screen
 */
void idle_screen_clear(void);

/**
 * @brief  Clear idle screen
 */
void idle_screen_clear(void);
