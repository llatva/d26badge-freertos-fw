/*
 * UI Test screen – comprehensive hardware testing
 *
 * Single-screen layout that simultaneously tests:
 *   • Display: colour bars covering the RGB gamut
 *   • Buttons: all 9 buttons shown as boxes, highlighted when pressed
 *   • LEDs:    SK6812 chain driven with a cycling rainbow
 *
 * Exit: press B + START together (shown on screen).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  phase;          /* Animation phase counter */
    bool     needs_full_draw; /* Set on init to force first full draw */
    bool     wants_exit;     /* Set when exit combo detected */
    bool     btn_state[9];   /* Cached per-frame button state */
    bool     btn_prev[9];    /* Previous frame button state (for dirty detection) */
} ui_test_screen_t;

/**
 * @brief  Initialise the UI test screen state.
 */
void ui_test_screen_init(ui_test_screen_t *screen);

/**
 * @brief  Draw one frame (called at ~33 FPS from display task).
 *         Polls buttons_is_pressed() internally for real-time feedback.
 *         Drives SK6812 LEDs with a rainbow cycle.
 */
void ui_test_screen_draw(ui_test_screen_t *screen);

/**
 * @brief  Returns true when the user has requested exit (B+START combo).
 */
bool ui_test_screen_wants_exit(const ui_test_screen_t *screen);

/**
 * @brief  Clear screen on exit.
 */
void ui_test_screen_clear(void);
