/*
 * Text input screen implementation
 */

#include "text_input_screen.h"
#include "st7789.h"
#include "buttons.h"
#include "badge_settings.h"
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
#define COLOR_INPUT_BG  0x1082  /* Dark blue */

/* Drawing constants */
#define INPUT_FONT_SCALE  2
#define CHAR_W            (8 * INPUT_FONT_SCALE)   /* 16 px per char */
#define CHAR_H            (8 * INPUT_FONT_SCALE)   /* 16 px per char (font is 8x8 base) */
#define INPUT_X           8
#define INPUT_Y           40
#define INPUT_BOX_X       4
#define INPUT_BOX_Y       35
#define INPUT_BOX_W       312
#define INPUT_BOX_H       34
#define CURSOR_Y          (INPUT_Y + CHAR_H + 2)   /* 58 */
#define CURSOR_H          2

/* Auto-repeat for held UP/DOWN: initial delay then fast repeat */
#define REPEAT_INITIAL_MS  300
#define REPEAT_FAST_MS     60

/* Track state for dirty-region drawing */
static bool s_needs_full_draw = true;
static uint32_t s_repeat_start_tick = 0;
static uint32_t s_last_repeat_tick = 0;
static int s_repeat_btn = -1;  /* BTN_UP or BTN_DOWN when held, -1 otherwise */

void text_input_init(text_input_screen_t *screen, const char *prompt, uint8_t max_len) {
    memset(screen, 0, sizeof(*screen));
    screen->prompt = prompt;
    screen->max_len = (max_len > TEXT_INPUT_MAX_LEN) ? TEXT_INPUT_MAX_LEN : max_len;
    screen->editing = true;
    screen->cursor_pos = 0;
    screen->buffer[0] = 'a';  /* Start with 'a' */
    s_needs_full_draw = true;
    s_repeat_btn = -1;
}

void text_input_set_text(text_input_screen_t *screen, const char *text) {
    if (!text) return;
    strncpy(screen->buffer, text, screen->max_len - 1);
    screen->buffer[screen->max_len - 1] = '\0';
    screen->cursor_pos = strlen(screen->buffer);
    if (screen->cursor_pos > 0) screen->cursor_pos--;  /* cursor on last real char */
    s_needs_full_draw = true;
}

/* ── Helper: cycle the character at cursor position ── */
static void cycle_char(text_input_screen_t *screen, int direction) {
    char current_char = screen->buffer[screen->cursor_pos];
    int char_idx = 0;

    for (int i = 0; i < (int)CHARSET_LEN; i++) {
        if (charset[i] == current_char) {
            char_idx = i;
            break;
        }
    }

    if (direction > 0) {
        char_idx = (char_idx + 1) % CHARSET_LEN;
    } else {
        char_idx = (char_idx == 0) ? (CHARSET_LEN - 1) : (char_idx - 1);
    }
    screen->buffer[screen->cursor_pos] = charset[char_idx];
}

void text_input_handle_button(text_input_screen_t *screen, int button_id) {
    if (!screen->editing) return;

    switch (button_id) {
    case BTN_UP:
        cycle_char(screen, +1);
        /* Start auto-repeat tracking */
        s_repeat_btn = BTN_UP;
        s_repeat_start_tick = xTaskGetTickCount();
        s_last_repeat_tick = s_repeat_start_tick;
        break;

    case BTN_DOWN:
        cycle_char(screen, -1);
        s_repeat_btn = BTN_DOWN;
        s_repeat_start_tick = xTaskGetTickCount();
        s_last_repeat_tick = s_repeat_start_tick;
        break;

    case BTN_LEFT:
        if (screen->cursor_pos > 0) {
            screen->cursor_pos--;
        }
        s_repeat_btn = -1;
        break;

    case BTN_RIGHT:
        if (screen->cursor_pos < screen->max_len - 2) {
            if (screen->buffer[screen->cursor_pos + 1] == '\0') {
                screen->buffer[screen->cursor_pos + 1] = ' ';
                screen->buffer[screen->cursor_pos + 2] = '\0';
            }
            screen->cursor_pos++;
        } else if (screen->cursor_pos < (uint8_t)strlen(screen->buffer) - 1) {
            screen->cursor_pos++;
        }
        s_repeat_btn = -1;
        break;

    case BTN_B:
        if (screen->cursor_pos > 0) {
            uint8_t len = strlen(screen->buffer);
            for (int i = screen->cursor_pos - 1; i < len; i++) {
                screen->buffer[i] = screen->buffer[i + 1];
            }
            screen->cursor_pos--;
        }
        s_repeat_btn = -1;
        break;

    case BTN_A:
    case BTN_STICK:
    case BTN_SELECT:
        screen->editing = false;
        s_repeat_btn = -1;
        break;

    default:
        break;
    }
}

void text_input_draw(text_input_screen_t *screen) {
    uint16_t TEXT_COL = settings_get_text_color();
    uint16_t ACCENT   = settings_get_accent_color();

    /* ── Auto-repeat for held UP/DOWN ── */
    if (s_repeat_btn == BTN_UP || s_repeat_btn == BTN_DOWN) {
        if (buttons_is_pressed((btn_id_t)s_repeat_btn)) {
            uint32_t now = xTaskGetTickCount();
            uint32_t elapsed = (now - s_repeat_start_tick) * portTICK_PERIOD_MS;
            if (elapsed > REPEAT_INITIAL_MS) {
                uint32_t since_last = (now - s_last_repeat_tick) * portTICK_PERIOD_MS;
                if (since_last >= REPEAT_FAST_MS) {
                    cycle_char(screen, (s_repeat_btn == BTN_UP) ? +1 : -1);
                    s_last_repeat_tick = now;
                }
            }
        } else {
            /* Button released */
            s_repeat_btn = -1;
        }
    }

    /* ── Full redraw (first time or after init) ── */
    if (s_needs_full_draw) {
        s_needs_full_draw = false;

        st7789_fill(COLOR_BG);

        /* Prompt */
        st7789_draw_string(4, 10, (char *)screen->prompt, TEXT_COL, COLOR_BG, 1);

        /* Input box background */
        st7789_fill_rect(INPUT_BOX_X, INPUT_BOX_Y, INPUT_BOX_W, INPUT_BOX_H, COLOR_INPUT_BG);

        /* Help text */
        st7789_draw_string(4, 70, "UP/DOWN: char  L/R: pos  B: del  A: done",
                           TEXT_COL, COLOR_BG, 1);
    }

    /* ── Incremental update: input area only ── */

    /* Clear the text area inside the input box */
    st7789_fill_rect(INPUT_X, INPUT_Y, INPUT_BOX_W - 8, CHAR_H, COLOR_INPUT_BG);

    /* Draw the buffer text */
    st7789_draw_string(INPUT_X, INPUT_Y, screen->buffer, TEXT_COL, COLOR_INPUT_BG, INPUT_FONT_SCALE);

    /* Clear old cursor line and draw new one */
    st7789_fill_rect(INPUT_BOX_X, CURSOR_Y, INPUT_BOX_W, CURSOR_H, COLOR_INPUT_BG);
    uint16_t cursor_x = INPUT_X + (screen->cursor_pos * CHAR_W);
    st7789_fill_rect(cursor_x, CURSOR_Y, CHAR_W, CURSOR_H, ACCENT);

    /* Length indicator */
    char buf[32];
    snprintf(buf, sizeof(buf), "Len: %u/%u  ", (uint8_t)strlen(screen->buffer), screen->max_len - 1);
    st7789_draw_string(4, 90, buf, TEXT_COL, COLOR_BG, 1);
}

const char *text_input_get_text(text_input_screen_t *screen) {
    return screen->buffer;
}

bool text_input_is_editing(text_input_screen_t *screen) {
    return screen->editing;
}
