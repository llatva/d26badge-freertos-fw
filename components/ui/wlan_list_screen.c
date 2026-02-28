/*
 * WLAN Networks List screen – scans and displays nearby WiFi networks.
 *
 * WiFi is expected to already be initialised in STA mode by app_main().
 * This module only starts/stops scanning and renders results.
 *
 * Layout (320 × 170):
 *   ┌────────────────────────────────────────────────┐  y=0
 *   │  WiFi Networks          Found: N               │  y=2  (title)
 *   ├────────────────────────────────────────────────┤  y=18 (divider)
 *   │  SSID_NAME           Ch 6   -45 dBm  L         │  y=20..
 *   │  …                                             │
 *   │  (up to 9 visible rows with scrolling)          │
 *   ├────────────────────────────────────────────────┤  y=156
 *   │  UP/DOWN: scroll   B: exit                     │
 *   └────────────────────────────────────────────────┘
 */

#include "wlan_list_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "buttons.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "wlan_list"

/* ── Colours ─────────────────────────────────────────────────────────────── */
#define COL_BG       0x0000u   /* black */
#define COL_WHITE    0xFFFFu
#define COL_WEAK     0xF800u   /* red — poor signal */
#define COL_MED      0xFFE0u   /* yellow — moderate signal */
#define COL_GOOD     0x07E0u   /* green — strong signal */
#define COL_DIM      0x4208u   /* dark gray */

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define TITLE_Y       2
#define DIVIDER_Y     18
#define LIST_Y_START  21
#define ROW_H         15
#define MAX_VISIBLE   9        /* rows that fit between divider and footer */
#define FOOTER_Y      156

/* ── Scan task ───────────────────────────────────────────────────────────── */
static volatile bool s_scanning = false;
static TaskHandle_t  s_scan_task = NULL;

static void wifi_scan_task(void *arg) {
    wlan_list_screen_t *screen = (wlan_list_screen_t *)arg;

    wifi_scan_config_t cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,          /* all channels */
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    ESP_LOGI(TAG, "Scan task started");

    while (s_scanning) {
        esp_err_t err = esp_wifi_scan_start(&cfg, true);   /* blocking */
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "scan_start failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count > MAX_WLAN_NETWORKS) ap_count = MAX_WLAN_NETWORKS;

        if (ap_count > 0) {
            wifi_ap_record_t ap_buf[MAX_WLAN_NETWORKS];
            esp_wifi_scan_get_ap_records(&ap_count, ap_buf);

            /* Copy results into screen struct (safe: display only reads) */
            for (uint16_t i = 0; i < ap_count; i++) {
                strncpy(screen->networks[i].ssid, (char *)ap_buf[i].ssid, 32);
                screen->networks[i].ssid[32] = '\0';
                screen->networks[i].rssi    = ap_buf[i].rssi;
                screen->networks[i].channel = ap_buf[i].primary;
                screen->networks[i].auth    = ap_buf[i].authmode;
            }
        }
        screen->num_networks = ap_count;
        screen->scan_done    = true;

        vTaskDelay(pdMS_TO_TICKS(3000));   /* re-scan every 3 s */
    }

    ESP_LOGI(TAG, "Scan task ending");
    s_scan_task = NULL;
    vTaskDelete(NULL);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════════════ */

void wlan_list_screen_init(wlan_list_screen_t *screen) {
    if (!screen) return;
    memset(screen, 0, sizeof(*screen));
    screen->needs_full_draw = true;
}

void wlan_list_screen_start_scan(wlan_list_screen_t *screen) {
    if (s_scanning) return;           /* already running */
    s_scanning = true;
    xTaskCreatePinnedToCore(wifi_scan_task, "wl_scan", 4096,
                            screen, 3, &s_scan_task, PRO_CPU_NUM);
}

void wlan_list_screen_stop_scan(void) {
    s_scanning = false;
    /* Wait up to 2 s for the task to self-delete */
    for (int i = 0; i < 40 && s_scan_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* Abort any in-progress scan so WiFi is quiet */
    esp_wifi_scan_stop();
}

/* ── Signal-strength colour helper ───────────────────────────────────────── */
static inline uint16_t rssi_color(int8_t rssi) {
    if (rssi >= -55) return COL_GOOD;
    if (rssi >= -75) return COL_MED;
    return COL_WEAK;
}

void wlan_list_screen_draw(wlan_list_screen_t *screen) {
    uint16_t accent = settings_get_accent_color();
    uint16_t text   = settings_get_text_color();
    bool full = screen->needs_full_draw;

    /* ── Full draw: title, divider, footer ──────────────────────────── */
    if (full) {
        st7789_fill(COL_BG);
        st7789_draw_string(4, TITLE_Y, "WiFi Networks", accent, COL_BG, 1);
        st7789_fill_rect(0, DIVIDER_Y, ST7789_WIDTH, 1, accent);
        st7789_draw_string(4, FOOTER_Y, "UP/DOWN scroll  B=exit", COL_DIM, COL_BG, 1);
        screen->needs_full_draw = false;
    }

    /* ── Status: network count (right-aligned) ──────────────────────── */
    {
        char buf[20];
        if (!screen->scan_done) {
            snprintf(buf, sizeof(buf), "Scanning...");
        } else {
            snprintf(buf, sizeof(buf), "Found: %d", screen->num_networks);
        }
        /* Clear old text area then draw */
        st7789_fill_rect(200, TITLE_Y, 120, 14, COL_BG);
        st7789_draw_string(200, TITLE_Y, buf, text, COL_BG, 1);
    }

    /* ── Network rows ───────────────────────────────────────────────── */
    uint8_t n = screen->num_networks;
    for (uint8_t vi = 0; vi < MAX_VISIBLE; vi++) {
        uint16_t y = LIST_Y_START + vi * ROW_H;
        uint8_t idx = screen->scroll_offset + vi;

        /* Clear row */
        st7789_fill_rect(0, y, ST7789_WIDTH, ROW_H, COL_BG);

        if (idx >= n) continue;

        wlan_network_info_t *net = &screen->networks[idx];
        uint16_t col = rssi_color(net->rssi);

        /* SSID (truncated to 20 chars) */
        char ssid_buf[22];
        strncpy(ssid_buf, net->ssid, 20);
        ssid_buf[20] = '\0';
        if (ssid_buf[0] == '\0') strncpy(ssid_buf, "<hidden>", sizeof(ssid_buf));
        st7789_draw_string(4, y, ssid_buf, col, COL_BG, 1);

        /* Channel */
        char ch_buf[8];
        snprintf(ch_buf, sizeof(ch_buf), "Ch%d", net->channel);
        st7789_draw_string(180, y, ch_buf, text, COL_BG, 1);

        /* RSSI */
        char rssi_buf[10];
        snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", net->rssi);
        st7789_draw_string(230, y, rssi_buf, col, COL_BG, 1);

        /* Lock indicator */
        if (net->auth != WIFI_AUTH_OPEN) {
            st7789_draw_string(300, y, "L", COL_DIM, COL_BG, 1);
        }
    }
}

void wlan_list_screen_handle_button(wlan_list_screen_t *screen, int button_id) {
    if (!screen) return;
    if (button_id == BTN_UP) {
        if (screen->scroll_offset > 0) screen->scroll_offset--;
    } else if (button_id == BTN_DOWN) {
        if (screen->scroll_offset + MAX_VISIBLE < screen->num_networks) {
            screen->scroll_offset++;
        }
    }
}

void wlan_list_screen_exit(void) {
    wlan_list_screen_stop_scan();
    ESP_LOGI(TAG, "Exited WLAN networks list screen");
}
