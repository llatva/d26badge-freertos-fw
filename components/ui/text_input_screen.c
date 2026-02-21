/*
 * Text input screen implementation
 */

#include "text_input_screen.h"
#include "st7789.h"
#include "buttons.h"
#include "badge_settings.h" /* New */
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

#define TAG "text_input"

/* Character set for input (space + alphanumeric) */
static const char charset[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
#define CHARSET_LEN (sizeof(charset) - 1)

/* Colors */
#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */
#define DEFAULT_COLOR_CURSOR    0x07E0  /* Green */
#define COLOR_INPUT_BG  0x1082  /* Dark blue */

void text_input_init(text_input_screen_t *screen, const char *prompt, uint8_t max_len) {
    memset(screen, 0, sizeof(*screen));
    screen->prompt = prompt;
    screen->max_len = (max_len > TEXT_INPUT_MAX_LEN) ? TEXT_INPUT_MAX_LEN : max_len;
    screen->editing = true;
    screen->cursor_pos = 0;
    screen->buffer[0] = 'a';  /* Start with 'a' */
}

void text_input_set_text(text_input_screen_t *screen, const char *text) {
    if (!text) return;
    strncpy(screen->buffer, text, screen->max_len - 1);
    screen->buffer[screen->max_len - 1] = '\0';
    screen->cursor_pos = strlen(screen->buffer);
}

void text_input_handle_button(text_input_screen_t *screen, int button_id) {
    if (!screen->editing) return;

    char current_char = screen->buffer[screen->cursor_pos];
    int char_idx = -1;

    /* Find current character in charset */
    for (int i = 0; i < CHARSET_LEN; i++) {
        if (charset[i] == current_char) {
            char_idx = i;
            break;
        }
    }
    if (char_idx == -1) char_idx = 0;  /* Default to first char if not found */

    switch (button_id) {
    case BTN_UP:
        /* Next character in charset */
        char_idx = (char_idx + 1) % CHARSET_LEN;
        screen->buffer[screen->cursor_pos] = charset[char_idx];
        break;

    case BTN_DOWN:
        /* Previous character in charset */
        char_idx = (char_idx == 0) ? (CHARSET_LEN - 1) : (char_idx - 1);
        screen->buffer[screen->cursor_pos] = charset[char_idx];
        break;

    case BTN_LEFT:
        /* Move cursor left */
        if (screen->cursor_pos > 0) {
            screen->cursor_pos--;
        }
        break;

    case BTN_RIGHT:
        /* Move cursor right, extend if at end (up to max_len - 1) */
        if (screen->cursor_pos < screen->max_len - 2) {
            if (screen->buffer[screen->cursor_pos + 1] == '\0') {
                /* At the very end of string, create new char */
                screen->buffer[screen->cursor_pos + 1] = ' '; // Start new char as space
                screen->buffer[screen->cursor_pos + 2] = '\0';
            }
            screen->cursor_pos++;
        } else if (screen->cursor_pos < strlen(screen->buffer) - 1) {
            /* Not at the very end of string, just move */
            screen->cursor_pos++;
        }
        break;

    case BTN_B:
        /* Backspace: delete character before current cursor position */
        if (screen->cursor_pos > 0) {
            uint8_t len = strlen(screen->buffer);
            for (int i = screen->cursor_pos - 1; i < len; i++) {
                screen->buffer[i] = screen->buffer[i + 1];
            }
            screen->cursor_pos--;
        }
        break;

    case BTN_A:
    case BTN_STICK:
    case BTN_SELECT:
        /* Confirm/Done */
        screen->editing = false;
        break;

    default:
        break;
    }
}

void text_input_draw(text_input_screen_t *screen) {
    uint16_t TEXT   = settings_get_text_color();
    uint16_t ACCENT = settings_get_accent_color();

    st7789_fill(COLOR_BG);
    st7789_draw_string(4, 10, (char *)screen->prompt, TEXT, COLOR_BG, 1);
    st7789_fill_rect(4, 35, 312, 25, COLOR_INPUT_BG);
    st7789_draw_string(4, 70, "UP/DOWN: char  LEFT/RIGHT: pos  B: back  A: done", TEXT, COLOR_BG, 1);

    /* Clear input area and redraw text */
    st7789_fill_rect(8, 40, 304, 16, COLOR_INPUT_BG);
    st7789_draw_string(8, 40, screen->buffer, TEXT, COLOR_INPUT_BG, 2);

    /* Draw cursor line */
    uint16_t cursor_x = 8 + (screen->cursor_pos * 8 * 2);
    st7789_fill_rect(cursor_x, 62, 16, 2, ACCENT);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "Len: %u/%u", (uint8_t)strlen(screen->buffer), screen->max_len - 1);
    st7789_draw_string(4, 90, buf, TEXT, COLOR_BG, 1);
}

const char *text_input_get_text(text_input_screen_t *screen) {
    return screen->buffer;
}

bool text_input_is_editing(text_input_screen_t *screen) {
    return screen->editing;
}
