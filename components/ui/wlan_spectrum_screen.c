/*
 * WLAN Spectrum Analyzer – scans WiFi channels 1-13 and displays signal bars.
 *
 * WiFi is expected to already be initialised in STA mode by app_main().
 * This module only starts/stops per-channel scanning and renders a bar chart.
 *
 * Layout (320 × 170):
 *   ┌────────────────────────────────────────────────┐  y=0
 *   │  WiFi Spectrum                                 │  y=2  (title)
 *   ├────────────────────────────────────────────────┤  y=18 (divider)
 *   │  ▇▇  ▇▇  ▇▇  ▇▇  … 13 bars                  │  y=20..129
 *   │  1   2   3   4   5  … 13  (channel labels)    │  y=132
 *   │  -45 -72 …                (dBm labels)        │  y=144
 *   │  B=exit   Scanning ch N                        │  y=156
 *   └────────────────────────────────────────────────┘
 */

#include "wlan_spectrum_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "wlan_spectrum"

/* ── Colours ─────────────────────────────────────────────────────────────── */
#define COL_BG       0x0000u
#define COL_WHITE    0xFFFFu
#define COL_DIM      0x4208u
#define COL_RED      0xF800u
#define COL_YELLOW   0xFFE0u
#define COL_GREEN    0x07E0u

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define TITLE_Y       2
#define DIVIDER_Y     18
#define BAR_Y_TOP     22        /* top of bar area */
#define BAR_MAX_H     108       /* maximum bar height in px */
#define BAR_Y_BOT     (BAR_Y_TOP + BAR_MAX_H)   /* 130 */
#define CH_LABEL_Y    132       /* channel number row */
#define DBM_LABEL_Y   144       /* dBm value row */
#define FOOTER_Y      157       /* bottom status line */

#define NUM_CHANNELS  13
#define BAR_W         22        /* bar width in px */
#define BAR_GAP       2         /* gap between bars */
#define BAR_X_START   3         /* left margin */

/* ── Scan task ───────────────────────────────────────────────────────────── */
static volatile bool s_scanning  = false;
static volatile uint8_t s_cur_ch = 0;   /* channel being scanned (1-13), 0=idle */
static TaskHandle_t s_scan_task  = NULL;

static void wifi_spectrum_scan_task(void *arg) {
    wlan_spectrum_screen_t *scr = (wlan_spectrum_screen_t *)arg;

    wifi_scan_config_t cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 30,
        .scan_time.active.max = 80,
    };

    ESP_LOGI(TAG, "Spectrum scan task started");

    while (s_scanning) {
        for (uint8_t ch = 1; ch <= NUM_CHANNELS; ch++) {
            if (!s_scanning) break;

            s_cur_ch     = ch;
            cfg.channel  = ch;

            esp_err_t err = esp_wifi_scan_start(&cfg, true);
            if (err != ESP_OK) {
                scr->channel_rssi[ch - 1] = -100;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);

            int8_t best = -100;

            if (ap_count > 0) {
                /* Use a small stack buffer — we only need the strongest */
                uint16_t fetch = ap_count;
                if (fetch > 20) fetch = 20;
                wifi_ap_record_t buf[20];
                esp_wifi_scan_get_ap_records(&fetch, buf);

                for (uint16_t i = 0; i < fetch; i++) {
                    if (buf[i].rssi > best) best = buf[i].rssi;
                }
            }
            scr->channel_rssi[ch - 1] = best;

            vTaskDelay(pdMS_TO_TICKS(10));
        }
        s_cur_ch = 0;
        vTaskDelay(pdMS_TO_TICKS(200));   /* pause between sweeps */
    }

    ESP_LOGI(TAG, "Spectrum scan task ending");
    s_scan_task = NULL;
    vTaskDelete(NULL);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════════════ */

void wlan_spectrum_screen_init(wlan_spectrum_screen_t *screen) {
    if (!screen) return;
    memset(screen, 0, sizeof(*screen));
    screen->num_channels = NUM_CHANNELS;
    for (uint8_t i = 0; i < MAX_WIFI_CHANNELS; i++) {
        screen->channel_rssi[i] = -100;
    }
    screen->needs_full_draw = true;
}

void wlan_spectrum_screen_start_scan(wlan_spectrum_screen_t *screen) {
    if (s_scanning) return;
    s_scanning = true;
    xTaskCreatePinnedToCore(wifi_spectrum_scan_task, "ws_scan", 4096,
                            screen, 3, &s_scan_task, PRO_CPU_NUM);
}

void wlan_spectrum_screen_stop_scan(void) {
    s_scanning = false;
    for (int i = 0; i < 40 && s_scan_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    esp_wifi_scan_stop();
}

/* ── Signal-strength colour helper ───────────────────────────────────────── */
static inline uint16_t rssi_color(int8_t rssi) {
    if (rssi >= -55) return COL_GREEN;
    if (rssi >= -75) return COL_YELLOW;
    return COL_RED;
}

void wlan_spectrum_screen_draw(wlan_spectrum_screen_t *scr) {
    uint16_t accent = settings_get_accent_color();
    bool full = scr->needs_full_draw;

    /* ── Full draw: static elements ──────────────────────────────────── */
    if (full) {
        st7789_fill(COL_BG);
        st7789_draw_string(4, TITLE_Y, "WiFi Spectrum", accent, COL_BG, 1);
        st7789_fill_rect(0, DIVIDER_Y, ST7789_WIDTH, 1, accent);

        /* Channel number labels (drawn once) */
        for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", i + 1);
            uint16_t x = BAR_X_START + i * (BAR_W + BAR_GAP);
            /* Centre the label under the bar */
            uint16_t tx = x + (BAR_W - (uint16_t)strlen(buf) * 8) / 2;
            st7789_draw_string(tx, CH_LABEL_Y, buf, COL_WHITE, COL_BG, 1);
        }

        st7789_draw_string(4, FOOTER_Y, "B=exit", COL_DIM, COL_BG, 1);
        scr->needs_full_draw = false;
    }

    /* ── Bars (redrawn every frame, only the bar area) ────────────── */
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        uint16_t x = BAR_X_START + i * (BAR_W + BAR_GAP);
        int8_t rssi = scr->channel_rssi[i];

        /* Normalise: -100 dBm → 0 px, -30 dBm → BAR_MAX_H px */
        int16_t norm = rssi + 100;               /* 0..70 */
        if (norm < 0)  norm = 0;
        if (norm > 70) norm = 70;
        uint16_t h = (uint16_t)(norm * BAR_MAX_H) / 70;

        /* Clear entire bar column */
        st7789_fill_rect(x, BAR_Y_TOP, BAR_W, BAR_MAX_H, COL_BG);

        /* Draw bar from bottom up */
        if (h > 0) {
            uint16_t y = BAR_Y_BOT - h;
            st7789_fill_rect(x, y, BAR_W, h, rssi_color(rssi));
        }

        /* dBm label under channel number */
        char dbm[6];
        st7789_fill_rect(x, DBM_LABEL_Y, BAR_W, 12, COL_BG);
        if (rssi > -100) {
            snprintf(dbm, sizeof(dbm), "%d", rssi);
            uint16_t tx = x + (BAR_W - (uint16_t)strlen(dbm) * 8) / 2;
            st7789_draw_string(tx, DBM_LABEL_Y, dbm, COL_DIM, COL_BG, 1);
        }
    }

    /* ── Scanning indicator ──────────────────────────────────────────── */
    {
        char status[24];
        uint8_t ch = s_cur_ch;
        if (ch > 0) {
            snprintf(status, sizeof(status), "Scanning ch %d ", ch);
        } else {
            snprintf(status, sizeof(status), "              ");
        }
        st7789_fill_rect(100, FOOTER_Y, 160, 12, COL_BG);
        st7789_draw_string(100, FOOTER_Y, status, COL_DIM, COL_BG, 1);
    }

    scr->frame_count++;
}

void wlan_spectrum_screen_exit(void) {
    wlan_spectrum_screen_stop_scan();
    ESP_LOGI(TAG, "Exited WLAN spectrum screen");
}