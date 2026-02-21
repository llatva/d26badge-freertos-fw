#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

/* ── Badge-specific pin mapping ────────────────────────────────────────── */
#define ST7789_PIN_SCK   GPIO_NUM_4
#define ST7789_PIN_MOSI  GPIO_NUM_5
#define ST7789_PIN_MISO  GPIO_NUM_16   /* wired but not used */
#define ST7789_PIN_CS    GPIO_NUM_6
#define ST7789_PIN_DC    GPIO_NUM_15
#define ST7789_PIN_RST   GPIO_NUM_7
#define ST7789_PIN_BL    GPIO_NUM_19

#define ST7789_SPI_HOST  SPI2_HOST
#define ST7789_SPI_FREQ  (80 * 1000 * 1000)

/* ── Physical display dimensions ───────────────────────────────────────── */
#define ST7789_WIDTH     320
#define ST7789_HEIGHT    170

/* ── Colour helpers (RGB565, big-endian on wire) ────────────────────────── */
#define RGB565(r, g, b)  ((uint16_t)(((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | (((b) & 0xF8u) >> 3))

#define COLOR_BLACK   0x0000u
#define COLOR_WHITE   0xFFFFu
#define COLOR_RED     RGB565(255, 0,   0)
#define COLOR_GREEN   RGB565(0,   255, 0)
#define COLOR_BLUE    RGB565(0,   0,   255)
#define COLOR_YELLOW  RGB565(255, 255, 0)
#define COLOR_CYAN    RGB565(0,   255, 255)
#define COLOR_MAGENTA RGB565(255, 0,   255)
#define COLOR_ORANGE  RGB565(255, 128, 0)
#define COLOR_GRAY    RGB565(128, 128, 128)

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the SPI bus and the ST7789 controller.
 *         Must be called once before any drawing function.
 */
void st7789_init(void);

/**
 * @brief  Fill the entire display with a single RGB565 colour.
 */
void st7789_fill(uint16_t colour);

/**
 * @brief  Fill a rectangular region with a colour.
 */
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour);

/**
 * @brief  Draw a single pixel.
 */
void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t colour);

/**
 * @brief  Draw an ASCII character using the built-in 8×16 font.
 *         Returns the x position after the character.
 */
uint16_t st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief  Draw a null-terminated ASCII string.
 */
void st7789_draw_string(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief  Control backlight (true = on).
 */
void st7789_set_backlight(bool on);
