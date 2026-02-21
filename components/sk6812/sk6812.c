/*
 * SK6812 RGB LED driver using ESP-IDF RMT peripheral.
 *
 * The SK6812 uses a single-wire protocol compatible with WS2812B:
 *   T0H ≈ 300 ns,  T0L ≈ 900 ns
 *   T1H ≈ 600 ns,  T1L ≈ 600 ns
 *   RESET ≥ 80 µs (low)
 *
 * We use the new ESP-IDF 5.x RMT encoder API (rmt_new_bytes_encoder).
 *
 * LED order on badge: pixels 0–7, data chain on GPIO18,
 * power enable on GPIO17 (active high).
 */

#include "sk6812.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "sk6812"

/* RMT resolution: 10 MHz → 100 ns per tick */
#define RMT_RESOLUTION_HZ   10000000UL
#define T0H_TICKS   3    /* 300 ns */
#define T0L_TICKS   9    /* 900 ns */
#define T1H_TICKS   6    /* 600 ns */
#define T1L_TICKS   6    /* 600 ns */
#define RESET_TICKS 1000 /* 100 µs  */

/* ── Module state ───────────────────────────────────────────────────────── */
static rmt_channel_handle_t s_chan = NULL;
static rmt_encoder_handle_t s_enc  = NULL;
static sk6812_color_t s_buf[SK6812_LED_COUNT];

/* Byte encoder config (reused for every show()) */
static rmt_bytes_encoder_config_t s_enc_cfg;
static rmt_transmit_config_t s_tx_cfg = { .loop_count = 0 };

/* ── Init ────────────────────────────────────────────────────────────────── */
void sk6812_init(void) {
    /* Power enable pin */
    gpio_config_t en = {
        .pin_bit_mask = 1ULL << SK6812_ENABLE_PIN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&en);
    gpio_set_level(SK6812_ENABLE_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* RMT TX channel */
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num         = SK6812_DATA_PIN,
        .clk_src          = RMT_CLK_SRC_DEFAULT,
        .resolution_hz    = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&chan_cfg, &s_chan));

    /* Bytes encoder */
    s_enc_cfg = (rmt_bytes_encoder_config_t){
        .bit0 = { .level0 = 1, .duration0 = T0H_TICKS,
                  .level1 = 0, .duration1 = T0L_TICKS },
        .bit1 = { .level0 = 1, .duration0 = T1H_TICKS,
                  .level1 = 0, .duration1 = T1L_TICKS },
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&s_enc_cfg, &s_enc));
    ESP_ERROR_CHECK(rmt_enable(s_chan));

    memset(s_buf, 0, sizeof(s_buf));
    sk6812_show();
    ESP_LOGI(TAG, "SK6812 ready (%d LEDs on GPIO%d)", SK6812_LED_COUNT, SK6812_DATA_PIN);
}

/* ── Pixel buffer operations ────────────────────────────────────────────── */
void sk6812_set(uint8_t index, sk6812_color_t c) {
    if (index < SK6812_LED_COUNT) s_buf[index] = c;
}

void sk6812_fill(sk6812_color_t c) {
    for (int i = 0; i < SK6812_LED_COUNT; i++) s_buf[i] = c;
}

void sk6812_clear(void) {
    sk6812_fill(SK6812_BLACK);
    sk6812_show();
}

/* ── Transmit ────────────────────────────────────────────────────────────── */
void sk6812_show(void) {
    /*
     * SK6812 byte order is GRB.
     * Build a raw GRB buffer from our internal RGB buffer.
     */
    uint8_t grb[SK6812_LED_COUNT * 3];
    for (int i = 0; i < SK6812_LED_COUNT; i++) {
        grb[i * 3 + 0] = s_buf[i].g;
        grb[i * 3 + 1] = s_buf[i].r;
        grb[i * 3 + 2] = s_buf[i].b;
    }

    rmt_transmit(s_chan, s_enc, grb, sizeof(grb), &s_tx_cfg);
    rmt_tx_wait_all_done(s_chan, pdMS_TO_TICKS(100));
}

/* ── Utility ─────────────────────────────────────────────────────────────── */
sk6812_color_t sk6812_scale(sk6812_color_t c, uint8_t brightness) {
    return (sk6812_color_t){
        .r = (uint8_t)((c.r * brightness) / 255),
        .g = (uint8_t)((c.g * brightness) / 255),
        .b = (uint8_t)((c.b * brightness) / 255),
    };
}
