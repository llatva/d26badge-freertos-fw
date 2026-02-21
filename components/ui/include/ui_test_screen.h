/*
 * UI Test screen – comprehensive hardware testing
 * Tests LEDs, buttons, and display with colorful patterns
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t mode;           /* Current test mode (0-3) */
    uint8_t phase;          /* Animation phase */
    uint32_t frame_count;   /* Frames rendered */
    bool updating;          /* Test is active */
} ui_test_screen_t;

/**
 * @brief  Initialize UI test screen
 */
void ui_test_screen_init(ui_test_screen_t *screen);

/**
 * @brief  Draw UI test screen with current pattern
 */
void ui_test_screen_draw(ui_test_screen_t *screen);

/**
 * @brief  Handle button press during test (cycles modes, exits on SELECT)
 */
void ui_test_screen_handle_button(ui_test_screen_t *screen, int button_id);

/**
 * @brief  Clear UI test screen
 */
void ui_test_screen_clear(void);

/**
 * @brief  Get currently tested button name for display
 */
const char *ui_test_get_button_name(int button_id);
