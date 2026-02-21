/*
 * Text input screen for badge settings
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Maximum length for editable strings */
#define TEXT_INPUT_MAX_LEN 32

/* Text input screen state */
typedef struct {
    char buffer[TEXT_INPUT_MAX_LEN];
    uint8_t cursor_pos;
    uint8_t max_len;
    const char *prompt;
    bool editing;
} text_input_screen_t;

/**
 * @brief  Initialize text input screen
 */
void text_input_init(text_input_screen_t *screen, const char *prompt, uint8_t max_len);

/**
 * @brief  Set initial text in input buffer
 */
void text_input_set_text(text_input_screen_t *screen, const char *text);

/**
 * @brief  Handle button input (UP/DOWN/LEFT/RIGHT/A/SELECT)
 *         UP/DOWN: change character
 *         LEFT/RIGHT: move cursor
 *         A: confirm input
 *         SELECT: cancel
 */
void text_input_handle_button(text_input_screen_t *screen, int button_id);

/**
 * @brief  Draw text input screen to display
 */
void text_input_draw(text_input_screen_t *screen);

/**
 * @brief  Get current buffer content
 */
const char *text_input_get_text(text_input_screen_t *screen);

/**
 * @brief  Get editing status
 */
bool text_input_is_editing(text_input_screen_t *screen);
