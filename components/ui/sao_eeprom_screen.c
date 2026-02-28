/*
 * SAO EEPROM screen – reads I2C EEPROM at 0x50 and displays hex + ASCII dump.
 *
 * Uses ESP-IDF new I2C master API (v5.x).
 * GPIO9 = SDA, GPIO10 = SCL  (standard SAO connector on Disobey badge).
 *
 * The screen reads 256 bytes on init, then displays them in a scrollable
 * hex-dump view:  ADDR  HH HH HH HH HH HH HH HH  CCCCCCCC
 */

#include "sao_eeprom_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include <stdio.h>
#include <string.h>

#define TAG "sao_eeprom"

/* ── I2C configuration ───────────────────────────────────────────────────── */
#define SAO_I2C_SDA_PIN      GPIO_NUM_9
#define SAO_I2C_SCL_PIN      GPIO_NUM_10
#define SAO_I2C_FREQ_HZ      100000       /* 100 kHz – safe for any EEPROM */
#define SAO_EEPROM_ADDR      0x50         /* 7-bit address of AT24Cxx etc. */
#define SAO_I2C_TIMEOUT_MS   100

/* ── Display layout ──────────────────────────────────────────────────────── */
#define COLOR_BG        0x0000  /* Black */
#define COLOR_ADDR      0x07E0  /* Green – address column */
#define COLOR_HEX       0xFFFF  /* White – hex bytes */
#define COLOR_ASCII     0xFFE0  /* Yellow – ASCII chars */
#define COLOR_DOT       0x7BEF  /* Gray – non-printable dot */
#define COLOR_ERR       0xF800  /* Red – error messages */

/* 8 bytes per line.  Each line: "XXXX  HH HH HH HH HH HH HH HH  CCCCCCCC"
 * At font scale 1 (8px wide) that's  4+2+3*8+1+8 = 39 chars => 312px, fits 320px */
#define BYTES_PER_LINE  8
#define TITLE_H         20      /* Title bar height */
#define LINE_H          16      /* Line height at scale 1 */
/* Usable vertical space: 170 - TITLE_H - 16(nav bar) = 134px => 8 lines */
#define VISIBLE_LINES   8

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static int total_lines(const sao_eeprom_screen_t *scr)
{
    int n = scr->bytes_read / BYTES_PER_LINE;
    if (scr->bytes_read % BYTES_PER_LINE) n++;
    return n;
}

/* ── Init: set up I2C, read EEPROM, tear down ───────────────────────────── */

void sao_eeprom_screen_init(sao_eeprom_screen_t *scr)
{
    if (!scr) return;

    memset(scr, 0, sizeof(*scr));

    ESP_LOGI(TAG, "Initialising I2C master (SDA=%d SCL=%d)", SAO_I2C_SDA_PIN, SAO_I2C_SCL_PIN);

    /* Create I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port       = I2C_NUM_0,
        .sda_io_num     = SAO_I2C_SDA_PIN,
        .scl_io_num     = SAO_I2C_SCL_PIN,
        .clk_source     = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        snprintf(scr->error_msg, sizeof(scr->error_msg),
                 "I2C bus init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", scr->error_msg);
        return;
    }

    /* Add EEPROM device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SAO_EEPROM_ADDR,
        .scl_speed_hz    = SAO_I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev = NULL;
    err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        snprintf(scr->error_msg, sizeof(scr->error_msg),
                 "I2C add device 0x%02X failed: %s", SAO_EEPROM_ADDR, esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", scr->error_msg);
        i2c_del_master_bus(bus);
        return;
    }

    /* Read EEPROM: send address byte 0x00 then read up to 256 bytes.
     * Many small EEPROMs (AT24C02) are 256 bytes with an 8-bit address.
     * We do a single sequential read starting at address 0. */
    uint8_t addr_byte = 0x00;
    err = i2c_master_transmit_receive(dev, &addr_byte, 1,
                                      scr->data, SAO_EEPROM_READ_SIZE,
                                      SAO_I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        scr->bytes_read = SAO_EEPROM_READ_SIZE;
        scr->read_ok = true;
        ESP_LOGI(TAG, "Read %d bytes from EEPROM at 0x%02X", scr->bytes_read, SAO_EEPROM_ADDR);
    } else {
        snprintf(scr->error_msg, sizeof(scr->error_msg),
                 "EEPROM read failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", scr->error_msg);
    }

    /* Clean up – we don't keep the bus open */
    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);
}

/* ── Draw ────────────────────────────────────────────────────────────────── */

void sao_eeprom_screen_draw(sao_eeprom_screen_t *scr)
{
    uint16_t ACCENT = settings_get_accent_color();
    st7789_fill(COLOR_BG);

    /* Title */
    st7789_draw_string(4, 2, "SAO / EEPROM", ACCENT, COLOR_BG, 2);

    if (!scr->read_ok) {
        /* Error state */
        st7789_draw_string(4, 40, "No SAO EEPROM found", COLOR_ERR, COLOR_BG, 1);
        st7789_draw_string(4, 60, scr->error_msg, COLOR_ERR, COLOR_BG, 1);
        st7789_draw_string(4, 90, "Addr: 0x50 (SDA=9 SCL=10)", 0x7BEF, COLOR_BG, 1);
        st7789_draw_string(4, 154, "B: back", ACCENT, COLOR_BG, 1);
        return;
    }

    int tl = total_lines(scr);

    /* Draw hex dump lines */
    for (int i = 0; i < VISIBLE_LINES; i++) {
        int line_idx = scr->scroll_offset + i;
        if (line_idx >= tl) break;

        int y = TITLE_H + i * LINE_H;
        int base = line_idx * BYTES_PER_LINE;

        /* Address column: "0080" */
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "%04X", base);
        st7789_draw_string(0, y, addr_str, COLOR_ADDR, COLOR_BG, 1);

        /* Hex bytes */
        int x_hex = 40;  /* 5 chars * 8px = 40px start */
        for (int j = 0; j < BYTES_PER_LINE; j++) {
            int idx = base + j;
            if (idx < scr->bytes_read) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X", scr->data[idx]);
                st7789_draw_string(x_hex, y, hex, COLOR_HEX, COLOR_BG, 1);
            }
            x_hex += 24;  /* 3 chars * 8px */
        }

        /* ASCII column */
        int x_asc = x_hex + 8;  /* 1-char gap after hex */
        for (int j = 0; j < BYTES_PER_LINE; j++) {
            int idx = base + j;
            if (idx < scr->bytes_read) {
                char ch = scr->data[idx];
                char c_str[2] = { (ch >= 0x20 && ch < 0x7F) ? ch : '.', '\0' };
                uint16_t color = (ch >= 0x20 && ch < 0x7F) ? COLOR_ASCII : COLOR_DOT;
                st7789_draw_string(x_asc, y, c_str, color, COLOR_BG, 1);
            }
            x_asc += 8;
        }
    }

    /* Scroll indicator */
    if (tl > VISIBLE_LINES) {
        int bar_total_h = VISIBLE_LINES * LINE_H;
        int bar_h = bar_total_h * VISIBLE_LINES / tl;
        if (bar_h < 6) bar_h = 6;
        int bar_y = TITLE_H;
        if (tl > VISIBLE_LINES) {
            bar_y = TITLE_H + (bar_total_h - bar_h) * scr->scroll_offset / (tl - VISIBLE_LINES);
        }
        for (int y = bar_y; y < bar_y + bar_h && y < TITLE_H + bar_total_h; y++) {
            st7789_draw_string(316, y, "|", 0x4208, COLOR_BG, 1);
        }
    }

    /* Nav bar */
    char info[32];
    snprintf(info, sizeof(info), "%d bytes", scr->bytes_read);
    st7789_draw_string(4, 154, "B:back", ACCENT, COLOR_BG, 1);
    st7789_draw_string(100, 154, info, 0x7BEF, COLOR_BG, 1);
    st7789_draw_string(220, 154, "U/D:scroll", 0x7BEF, COLOR_BG, 1);
}

/* ── Scroll ──────────────────────────────────────────────────────────────── */

void sao_eeprom_screen_scroll_up(sao_eeprom_screen_t *scr)
{
    if (scr->scroll_offset > 0) scr->scroll_offset--;
}

void sao_eeprom_screen_scroll_down(sao_eeprom_screen_t *scr)
{
    int tl = total_lines(scr);
    if (scr->scroll_offset < tl - VISIBLE_LINES) scr->scroll_offset++;
}
