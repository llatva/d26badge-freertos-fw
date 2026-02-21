/*
 * UI Test screen implementation – tests LEDs, buttons, display
 */

#include "ui_test_screen.h"
#include "st7789.h"
#include "sk6812.h"
#include "buttons.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define TAG "ui_test"

/* Colors */
#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */

/* Helper: scale an RGB565 color by a percentage (0-255) */
static inline uint16_t scale_rgb565(uint16_t color, uint8_t scale_pct) {
    /* Clamp scale_pct to 0-255 */
    if (scale_pct > 255) scale_pct = 255;
    
    /* Extract RGB565 components */
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    /* Scale each component (scale_pct is 0-255, so divide by 255) */
    r = (r * scale_pct) / 255;
    g = (g * scale_pct) / 255;
    b = (b * scale_pct) / 255;
    
    /* Repack as RGB565 */
    return (r << 11) | (g << 5) | b;
}

/* Test modes */
#define TEST_MODE_LED_RAINBOW   0
#define TEST_MODE_LED_INDIVIDUAL 1
#define TEST_MODE_COLOR_PATTERNS 2
#define TEST_MODE_BUTTON_TEST   3
#define NUM_TEST_MODES  4

static uint8_t s_last_button = 0xFF;
static const char *s_button_names[] = {
    "UP", "DOWN", "LEFT", "RIGHT", "STICK", "A", "B", "START", "SELECT"
};

void ui_test_screen_init(ui_test_screen_t *screen) {
    memset(screen, 0, sizeof(*screen));
    screen->mode = TEST_MODE_LED_RAINBOW;
    screen->frame_count = 0;
}

const char *ui_test_get_button_name(int button_id) {
    if (button_id >= 0 && button_id < 9) {
        return s_button_names[button_id];
    }
    return "???";
}

void ui_test_screen_handle_button(ui_test_screen_t *screen, int button_id) {
    if (button_id == BTN_SELECT || button_id == BTN_A) {
        /* Exit test */
        screen->updating = false;
        return;
    }
    
    if (button_id == BTN_RIGHT || button_id == BTN_DOWN) {
        /* Next test mode */
        screen->mode = (screen->mode + 1) % NUM_TEST_MODES;
        screen->phase = 0;
        screen->frame_count = 0;
        ESP_LOGI(TAG, "Switched to test mode %d", screen->mode);
    } else if (button_id == BTN_LEFT || button_id == BTN_UP) {
        /* Previous test mode */
        screen->mode = (screen->mode == 0) ? (NUM_TEST_MODES - 1) : (screen->mode - 1);
        screen->phase = 0;
        screen->frame_count = 0;
        ESP_LOGI(TAG, "Switched to test mode %d", screen->mode);
    } else {
        /* Record which button was pressed */
        s_last_button = button_id;
    }
}

static void draw_led_rainbow(ui_test_screen_t *screen) {
    st7789_fill(COLOR_BG);
    st7789_draw_string(4, 10, "LED Test: Rainbow", COLOR_TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 30, "Cycling rainbow through LEDs", COLOR_TEXT, COLOR_BG, 1);

    /* Draw LED representation */
    for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
        uint8_t hue = ((i * 32 + screen->phase) & 0xFF);
        
        /* Simple HSV to RGB conversion */
        uint8_t region = hue / 43;
        uint8_t remainder = (hue % 43) * 6;
        
        uint8_t r = 0, g = 0, b = 0;
        switch (region) {
            case 0: r = 255; g = remainder; break;
            case 1: r = 255 - remainder; g = 255; break;
            case 2: g = 255; b = remainder; break;
            case 3: g = 255 - remainder; b = 255; break;
            case 4: r = remainder; b = 255; break;
            case 5: r = 255; b = 255 - remainder; break;
        }
        
        /* Draw LED as small square */
        uint16_t x = 10 + (i % 4) * 35;
        uint16_t y = 55 + (i / 4) * 25;
        uint16_t color = (((r >> 3) & 0x1F) << 11) | (((g >> 2) & 0x3F) << 5) | ((b >> 3) & 0x1F);
        st7789_fill_rect(x, y, 30, 20, color);
        
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i);
        st7789_draw_string(x + 8, y + 5, buf, COLOR_BG, color, 1);
    }

    st7789_draw_string(4, 155, "LEFT/UP: prev  RIGHT/DOWN: next  SELECT: exit", COLOR_TEXT, COLOR_BG, 1);
    screen->phase++;
}

static void draw_led_individual(ui_test_screen_t *screen) {
    st7789_fill(COLOR_BG);
    st7789_draw_string(4, 10, "LED Test: Individual", COLOR_TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 30, "Each LED glows in sequence", COLOR_TEXT, COLOR_BG, 1);

    uint8_t active_led = screen->phase % SK6812_LED_COUNT;
    
    /* Draw LED grid with one glowing */
    for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
        uint16_t x = 10 + (i % 4) * 35;
        uint16_t y = 55 + (i / 4) * 25;
        
        uint16_t color;
        if (i == active_led) {
            /* Active LED: bright white */
            color = 0xFFFF;
        } else {
            /* Inactive LED: dim gray */
            color = 0x4208;
        }
        
        st7789_fill_rect(x, y, 30, 20, color);
        
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i);
        st7789_draw_string(x + 8, y + 5, buf, COLOR_BG, color, 1);
    }

    st7789_draw_string(4, 155, "LEFT/UP: prev  RIGHT/DOWN: next  SELECT: exit", COLOR_TEXT, COLOR_BG, 1);
    screen->phase++;
}

static void draw_color_patterns(ui_test_screen_t *screen) {
    st7789_fill(COLOR_BG);
    st7789_draw_string(4, 10, "Display Test: Color Patterns", COLOR_TEXT, COLOR_BG, 1);

    uint8_t pattern = (screen->phase / 20) % 6;
    
    uint16_t y_start = 35;
    uint16_t h_per_pattern = (170 - y_start - 20) / 6;
    
    static const uint16_t colors[] = {
        0xF800, /* Red */
        0x07E0, /* Green */
        0x001F, /* Blue */
        0xFFE0, /* Yellow */
        0xF81F, /* Magenta */
        0x07FF  /* Cyan */
    };
    
    for (uint8_t i = 0; i < 6; i++) {
        uint16_t y = y_start + i * h_per_pattern;
        uint16_t color = colors[i];
        
        /* Add brightness modulation to active pattern */
        if (i == pattern) {
            uint8_t brightness = 100 + (int8_t)(55 * sinf(screen->phase / 10.0f));
            color = scale_rgb565(color, brightness);
        }
        
        st7789_fill_rect(0, y, 320, h_per_pattern, color);
    }

    st7789_draw_string(4, 150, "TV test pattern. LEFT/UP/RIGHT/DOWN: prev/next", COLOR_BG, COLOR_BG, 1);
    screen->phase++;
}

static void draw_button_test(ui_test_screen_t *screen) {
    st7789_fill(COLOR_BG);
    st7789_draw_string(4, 10, "Button Test", COLOR_TEXT, COLOR_BG, 1);

    st7789_draw_string(4, 35, "Press any button to test", COLOR_TEXT, COLOR_BG, 1);
    
    if (s_last_button != 0xFF) {
        const char *btn_name = ui_test_get_button_name(s_last_button);
        char buf[32];
        snprintf(buf, sizeof(buf), "Last pressed: %s", btn_name);
        st7789_draw_string(4, 55, buf, 0x07E0, COLOR_BG, 2);
    }

    /* Draw button layout visualization */
    st7789_draw_string(4, 90, "Keypad Layout:", COLOR_TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 105, "  UP DOWN LEFT RIGHT    [STICK]", COLOR_TEXT, COLOR_BG, 1);
    st7789_draw_string(4, 120, "  A    B    START SELECT", COLOR_TEXT, COLOR_BG, 1);

    st7789_draw_string(4, 150, "LEFT/UP: prev mode  RIGHT/DOWN: next  SELECT: exit", COLOR_TEXT, COLOR_BG, 1);
}

void ui_test_screen_draw(ui_test_screen_t *screen) {
    switch (screen->mode) {
    case TEST_MODE_LED_RAINBOW:
        draw_led_rainbow(screen);
        break;
    case TEST_MODE_LED_INDIVIDUAL:
        draw_led_individual(screen);
        break;
    case TEST_MODE_COLOR_PATTERNS:
        draw_color_patterns(screen);
        break;
    case TEST_MODE_BUTTON_TEST:
        draw_button_test(screen);
        break;
    }
    
    screen->frame_count++;
}

void ui_test_screen_clear(void) {
    st7789_fill(0x0000);
}
