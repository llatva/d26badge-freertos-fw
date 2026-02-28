/*
 * UI Test screen – comprehensive hardware testing for Disobey Badge 2025/26.
 *
 * Single screen that tests everything at once:
 *
 *   ┌──────────────────────────────────────────────┐  y=0
 *   │  "HW TEST"  title + "B+START=exit"           │
 *   ├──────────────────────────────────────────────┤  y=18
 *   │  6 colour bars  R G B Y M C                  │  y=18..57  (6×7px)
 *   ├──────────────────────────────────────────────┤  y=60
 *   │  "BUTTONS:" label                            │  y=62
 *   │  ┌──────┐  ┌──────┐  ┌──────┐  … 9 boxes    │  y=78..164
 *   │  │  UP  │  │ DOWN │  │ LEFT │  etc.          │
 *   │  └──────┘  └──────┘  └──────┘                │
 *   └──────────────────────────────────────────────┘
 *
 *   Button boxes light up in accent colour when pressed.
 *   LEDs cycle through a rainbow on the SK6812 chain.
 *   Exit: press B + START simultaneously.
 *
 *   Incremental redraw: only buttons that changed state are redrawn.
 *   First frame draws everything.
 */

#include "ui_test_screen.h"
#include "st7789.h"
#include "sk6812.h"
#include "buttons.h"
#include <string.h>

/* ── Colours ─────────────────────────────────────────────────────────────── */
#define COL_BG       0x0000u   /* black */
#define COL_WHITE    0xFFFFu
#define COL_GRAY     0x4208u   /* dim gray for inactive button box */
#define COL_DK_GRAY  0x2104u   /* darker gray for box text when inactive */
#define COL_GREEN    0x07E0u   /* bright green for pressed button */
#define COL_ACCENT   0x07FFu   /* cyan accent for pressed button */
#define COL_RED      0xF800u
#define COL_BLUE     0x001Fu
#define COL_YELLOW   0xFFE0u
#define COL_MAGENTA  0xF81Fu
#define COL_CYAN     0x07FFu

/* ── Layout constants ────────────────────────────────────────────────────── */
#define TITLE_Y         1
#define BAR_Y           20
#define BAR_H           6
#define BAR_GAP         1
#define NUM_BARS        6

#define BTN_LABEL_Y     62
#define BTN_AREA_Y      78

/* Button box dimensions — 3 columns × 3 rows */
#define BOX_COLS        5
#define BOX_ROWS        2
#define BOX_W           58
#define BOX_H           40
#define BOX_PAD_X       6      /* horizontal gap */
#define BOX_PAD_Y       5      /* vertical gap */
#define BOX_X_START     3      /* left margin */

/* Button ordering in the grid (matches visual layout) */
/* Row 0: UP  DOWN  LEFT  RIGHT  STICK */
/* Row 1: A   B     START SELECT       */
static const uint8_t btn_grid[BOX_ROWS][BOX_COLS] = {
    { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_STICK },
    { BTN_A,  BTN_B,    BTN_START, BTN_SELECT, 0xFF /* empty */ }
};

static const char *btn_names[] = {
    "UP", "DOWN", "LEFT", "RIGHT", "STICK",
    "A", "B", "START", "SELECT"
};

/* Colour bar colours */
static const uint16_t bar_colors[NUM_BARS] = {
    COL_RED, 0x07E0, COL_BLUE, COL_YELLOW, COL_MAGENTA, COL_CYAN
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void draw_button_box(uint8_t row, uint8_t col, uint8_t btn_id, bool pressed) {
    uint16_t x = BOX_X_START + col * (BOX_W + BOX_PAD_X);
    uint16_t y = BTN_AREA_Y + row * (BOX_H + BOX_PAD_Y);

    uint16_t bg = pressed ? COL_GREEN : COL_GRAY;
    uint16_t fg = pressed ? COL_BG   : COL_DK_GRAY;

    /* Fill box */
    st7789_fill_rect(x, y, BOX_W, BOX_H, bg);

    /* Draw 1px border (brighter when pressed) */
    uint16_t border = pressed ? COL_WHITE : COL_DK_GRAY;
    st7789_fill_rect(x, y, BOX_W, 1, border);                   /* top */
    st7789_fill_rect(x, y + BOX_H - 1, BOX_W, 1, border);      /* bottom */
    st7789_fill_rect(x, y, 1, BOX_H, border);                   /* left */
    st7789_fill_rect(x + BOX_W - 1, y, 1, BOX_H, border);      /* right */

    /* Centre the label inside the box */
    const char *name = btn_names[btn_id];
    uint8_t len = (uint8_t)strlen(name);
    uint16_t tx = x + (BOX_W - len * 8) / 2;
    uint16_t ty = y + (BOX_H - 16) / 2;
    st7789_draw_string(tx, ty, name, fg, bg, 1);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void ui_test_screen_init(ui_test_screen_t *screen) {
    memset(screen, 0, sizeof(*screen));
    screen->needs_full_draw = true;
    screen->wants_exit = false;
    /* Set btn_prev to all-0xFF so every button is "dirty" on first frame */
    memset(screen->btn_prev, 0xFF, sizeof(screen->btn_prev));
}

void ui_test_screen_draw(ui_test_screen_t *screen) {
    /* ── 1. Poll all buttons ─────────────────────────────────────────── */
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        screen->btn_state[i] = buttons_is_pressed((btn_id_t)i);
    }

    /* ── 2. Check exit combo: B + START held simultaneously ────────── */
    if (screen->btn_state[BTN_B] && screen->btn_state[BTN_START]) {
        screen->wants_exit = true;
        return;
    }

    bool full = screen->needs_full_draw;

    /* ── 3. Full draw: static elements ───────────────────────────────── */
    if (full) {
        st7789_fill(COL_BG);

        /* Title */
        st7789_draw_string(4, TITLE_Y, "HW TEST", COL_WHITE, COL_BG, 1);
        st7789_draw_string(170, TITLE_Y, "Hold B+START = exit", COL_DK_GRAY, COL_BG, 1);

        /* Colour bars (static — drawn once) */
        uint16_t bar_total_h = NUM_BARS * BAR_H + (NUM_BARS - 1) * BAR_GAP;
        (void)bar_total_h;
        for (uint8_t i = 0; i < NUM_BARS; i++) {
            uint16_t y = BAR_Y + i * (BAR_H + BAR_GAP);
            st7789_fill_rect(0, y, ST7789_WIDTH, BAR_H, bar_colors[i]);
        }

        /* "BUTTONS:" label */
        st7789_draw_string(4, BTN_LABEL_Y, "BUTTONS:", COL_WHITE, COL_BG, 1);

        screen->needs_full_draw = false;
    }

    /* ── 4. Draw button boxes (incremental) ─────────────────────────── */
    for (uint8_t row = 0; row < BOX_ROWS; row++) {
        for (uint8_t col = 0; col < BOX_COLS; col++) {
            uint8_t btn_id = btn_grid[row][col];
            if (btn_id == 0xFF) continue;  /* empty cell */

            bool pressed = screen->btn_state[btn_id];
            bool prev    = screen->btn_prev[btn_id];

            /* Only redraw if state changed (or first frame) */
            if (full || pressed != prev) {
                draw_button_box(row, col, btn_id, pressed);
            }
        }
    }

    /* ── 5. Drive SK6812 LEDs with rainbow ──────────────────────────── */
    for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
        uint8_t hue = (uint8_t)(i * (256 / SK6812_LED_COUNT) + screen->phase);
        uint8_t region = hue / 43;
        uint8_t rem    = (hue % 43) * 6;
        uint8_t r = 0, g = 0, b = 0;
        switch (region) {
            case 0: r = 255; g = rem;         break;
            case 1: r = 255 - rem; g = 255;   break;
            case 2: g = 255; b = rem;         break;
            case 3: g = 255 - rem; b = 255;   break;
            case 4: r = rem; b = 255;         break;
            default: r = 255; b = 255 - rem;  break;
        }
        /* Dim to ~25% brightness so it's not blinding */
        sk6812_set(i, (sk6812_color_t){ r / 4, g / 4, b / 4 });
    }
    sk6812_show();

    /* ── 6. Save state for next frame ────────────────────────────────── */
    memcpy(screen->btn_prev, screen->btn_state, sizeof(screen->btn_prev));
    screen->phase += 2;
}

bool ui_test_screen_wants_exit(const ui_test_screen_t *screen) {
    return screen->wants_exit;
}

void ui_test_screen_clear(void) {
    /* Turn off LEDs */
    sk6812_clear();
    sk6812_show();
    st7789_fill(0x0000);
}
