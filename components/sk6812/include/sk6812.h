#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── Badge-specific constants ───────────────────────────────────────────── */
#define SK6812_LED_COUNT    12
#define SK6812_DATA_PIN     18   /* GPIO18 – RMT TX */
#define SK6812_ENABLE_PIN   17   /* GPIO17 – active-high power enable */

/* ── Colour type ─────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t r, g, b;
} sk6812_color_t;

/* Predefined colours */
#define SK6812_BLACK   ((sk6812_color_t){0,   0,   0  })
#define SK6812_RED     ((sk6812_color_t){255, 0,   0  })
#define SK6812_GREEN   ((sk6812_color_t){0,   255, 0  })
#define SK6812_BLUE    ((sk6812_color_t){0,   0,   255})
#define SK6812_WHITE   ((sk6812_color_t){255, 255, 255})
#define SK6812_YELLOW  ((sk6812_color_t){255, 255, 0  })
#define SK6812_CYAN    ((sk6812_color_t){0,   255, 255})
#define SK6812_MAGENTA ((sk6812_color_t){255, 0,   255})
#define SK6812_ORANGE  ((sk6812_color_t){255, 128, 0  })

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the RMT peripheral and enable the LED power rail.
 */
void sk6812_init(void);

/**
 * @brief  Write a colour to a specific LED index (0 = nearest to data pin).
 *         Call sk6812_show() to latch the data.
 */
void sk6812_set(uint8_t index, sk6812_color_t color);

/**
 * @brief  Set all LEDs to the same colour.
 */
void sk6812_fill(sk6812_color_t color);

/**
 * @brief  Turn all LEDs off.
 */
void sk6812_clear(void);

/**
 * @brief  Transmit the current pixel buffer to the LED chain.
 */
void sk6812_show(void);

/**
 * @brief  Scale a colour by a brightness factor 0–255.
 */
sk6812_color_t sk6812_scale(sk6812_color_t c, uint8_t brightness);
