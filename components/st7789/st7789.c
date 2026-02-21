/*
 * ST7789 SPI display driver for Disobey Badge 2025.
 *
 * Pinout (from HARDWARE.md):
 *   SCK  = GPIO4,  MOSI = GPIO5,  MISO = GPIO16 (unused)
 *   CS   = GPIO6,  DC   = GPIO15, RST  = GPIO7,  BL = GPIO19
 *
 * Display: 320 × 170 pixels, landscape, RGB565.
 *
 * This driver is intentionally minimal – it uses polling SPI transactions
 * (no DMA interrupts) so that it is safe to call from multiple tasks as long
 * as callers serialise access (the display task owns the bus).
 */

#include "st7789.h"
#include "font8x16.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

#define TAG "st7789"

/* ── ST7789 command set ─────────────────────────────────────────────────── */
#define ST7789_NOP     0x00
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT  0x11
#define ST7789_NORON   0x13
#define ST7789_INVON   0x21
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_COLMOD  0x3A
#define ST7789_MADCTL  0x36

/* MADCTL bits */
#define MADCTL_MX  0x40   /* column address order */
#define MADCTL_MY  0x80   /* row address order    */
#define MADCTL_MV  0x20   /* row/col exchange     */
#define MADCTL_RGB 0x00

/* ── Module-private state ───────────────────────────────────────────────── */
static spi_device_handle_t s_spi;

/* ── Low-level SPI helpers ──────────────────────────────────────────────── */
static inline void dc_set(bool data) {
    gpio_set_level(ST7789_PIN_DC, data ? 1 : 0);
}

static void spi_write_byte(uint8_t b) {
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &b,
        .flags = 0,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_write_buf(const void *buf, size_t len) {
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = buf,
        .flags = 0,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void cmd(uint8_t c) {
    dc_set(false);
    spi_write_byte(c);
}

static void data8(uint8_t d) {
    dc_set(true);
    spi_write_byte(d);
}

/* ── Address window ─────────────────────────────────────────────────────── */
/*
 * The ER-TFT019-1 / 1.9" 320×170 display has a Y-offset of 35 rows from the
 * ST7789 internal 320×240 frame buffer.  Without this offset every draw call
 * lands in the wrong position.
 */
#define COL_OFFSET 0
#define ROW_OFFSET 35

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint16_t cs = x0 + COL_OFFSET, ce = x1 + COL_OFFSET;
    uint16_t rs = y0 + ROW_OFFSET, re = y1 + ROW_OFFSET;

    cmd(ST7789_CASET);
    dc_set(true);
    spi_write_byte(cs >> 8); spi_write_byte(cs & 0xFF);
    spi_write_byte(ce >> 8); spi_write_byte(ce & 0xFF);

    cmd(ST7789_RASET);
    dc_set(true);
    spi_write_byte(rs >> 8); spi_write_byte(rs & 0xFF);
    spi_write_byte(re >> 8); spi_write_byte(re & 0xFF);

    cmd(ST7789_RAMWR);
    dc_set(true);
}

/* ── Public init ─────────────────────────────────────────────────────────── */
void st7789_init(void) {
    /* GPIO: DC, RST, BL */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ST7789_PIN_DC)  |
                        (1ULL << ST7789_PIN_RST)  |
                        (1ULL << ST7789_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    gpio_set_level(ST7789_PIN_BL, 0);  /* backlight off during init */
    gpio_set_level(ST7789_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(ST7789_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(ST7789_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num = ST7789_PIN_MOSI,
        .miso_io_num = ST7789_PIN_MISO,
        .sclk_io_num = ST7789_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7789_WIDTH * ST7789_HEIGHT * 2 + 8,
    };
    spi_bus_initialize(ST7789_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ST7789_SPI_FREQ,
        .mode = 2,               /* CPOL=1, CPHA=0 for ST7789 */
        .spics_io_num = ST7789_PIN_CS,
        .queue_size = 7,
        .pre_cb = NULL,
    };
    spi_bus_add_device(ST7789_SPI_HOST, &devcfg, &s_spi);

    /* ST7789 init sequence */
    cmd(ST7789_SWRESET); vTaskDelay(pdMS_TO_TICKS(150));
    cmd(ST7789_SLPOUT);  vTaskDelay(pdMS_TO_TICKS(10));

    cmd(ST7789_COLMOD); data8(0x55);   /* 16-bit RGB565 */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Landscape orientation: MV | MX  (matches MicroPython driver ADAFRUIT_1_9) */
    cmd(ST7789_MADCTL); data8(MADCTL_MV | MADCTL_MX | MADCTL_RGB);

    cmd(ST7789_INVON);               /* IPS panel requires inversion */
    vTaskDelay(pdMS_TO_TICKS(10));
    cmd(ST7789_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));
    cmd(ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(10));

    st7789_fill(COLOR_BLACK);
    st7789_set_backlight(true);

    ESP_LOGI(TAG, "ST7789 ready (%d×%d)", ST7789_WIDTH, ST7789_HEIGHT);
}

/* ── Drawing primitives ─────────────────────────────────────────────────── */
void st7789_fill(uint16_t colour) {
    st7789_fill_rect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, colour);
}

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour) {
    if (w == 0 || h == 0) return;

    /* Pre-swap bytes for big-endian wire format */
    uint16_t c = (colour >> 8) | (colour << 8);

    /* Build a one-row buffer on the stack (640 bytes max) */
    static uint16_t row_buf[ST7789_WIDTH];
    for (uint16_t i = 0; i < w && i < ST7789_WIDTH; i++) row_buf[i] = c;

    set_window(x, y, x + w - 1, y + h - 1);
    for (uint16_t r = 0; r < h; r++) {
        spi_write_buf(row_buf, w * 2);
    }
}

void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t colour) {
    set_window(x, y, x, y);
    uint8_t buf[2] = { colour >> 8, colour & 0xFF };
    spi_write_buf(buf, 2);
}

/* ── Text rendering (uses font8x16.h) ──────────────────────────────────── */
uint16_t st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale) {
    if (scale < 1) scale = 1;
    if ((uint8_t)c < 32 || (uint8_t)c > 127) c = '?';

    const uint8_t *glyph = font8x16_data[(uint8_t)c - 32];
    uint16_t char_w = (uint16_t)8 * scale;
    uint16_t char_h = (uint16_t)16 * scale;

    /* Buffer for scaled character (max 32×64 at scale 4) */
    static uint16_t char_buf[32 * 64];
    
    /* Pre-swap colors once */
    uint16_t fg_swapped = (fg >> 8) | (fg << 8);
    uint16_t bg_swapped = (bg >> 8) | (bg << 8);

    /* Render glyph into buffer row by row */
    uint16_t buf_idx = 0;
    for (uint8_t row = 0; row < 16; row++) {
        /* Repeat row 'scale' times vertically */
        for (uint8_t sr = 0; sr < scale; sr++) {
            /* Render columns */
            for (uint8_t col = 0; col < 8; col++) {
                uint16_t colour = (glyph[row] & (0x80 >> col)) ? fg_swapped : bg_swapped;
                /* Repeat pixel 'scale' times horizontally */
                for (uint8_t sc = 0; sc < scale; sc++) {
                    char_buf[buf_idx++] = colour;
                }
            }
        }
    }

    /* Verify we filled exactly the right amount */
    if (buf_idx != (char_w * char_h)) {
        return x + char_w;  /* error: bail out */
    }

    /* Send character to display in one SPI transaction */
    set_window(x, y, x + char_w - 1, y + char_h - 1);
    spi_write_buf(char_buf, buf_idx * 2);

    return x + char_w;
}

void st7789_draw_string(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale) {
    while (*s) {
        x = st7789_draw_char(x, y, *s++, fg, bg, scale);
    }
}

void st7789_set_backlight(bool on) {
    gpio_set_level(ST7789_PIN_BL, on ? 1 : 0);
}
