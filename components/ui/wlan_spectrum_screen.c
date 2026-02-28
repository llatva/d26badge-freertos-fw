/*
 * WLAN Spectrum Analyzer implementation – scans all WiFi channels and displays power in dBm
 */

#include "wlan_spectrum_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "buttons.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "wlan_spectrum"

#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */
#define COLOR_WEAK      0x4208  /* Dark gray */
#define COLOR_GOOD      0x07E0  /* Green */
#define COLOR_STRONG    0xFFE0  /* Yellow */

/* WiFi scanning state */
static TaskHandle_t s_scan_task = NULL;
static bool s_scanning = false;
static wlan_spectrum_screen_t *s_active_screen = NULL;

/* Background WiFi scanning task */
static void wifi_scan_task(void *arg) {
    wlan_spectrum_screen_t *screen = (wlan_spectrum_screen_t *)arg;
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,  /* Will be set per channel */
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 50,
        .scan_time.active.max = 100
    };

    ESP_LOGI(TAG, "WiFi scan task started");

    while (s_scanning) {
        /* Scan each channel individually */
        for (uint8_t ch = 1; ch <= 13; ch++) {
            if (!s_scanning) break;
            
            scan_config.channel = ch;
            
            /* Start scan on this channel */
            esp_err_t err = esp_wifi_scan_start(&scan_config, true);  /* Blocking */
            if (err == ESP_OK) {
                uint16_t ap_count = 0;
                esp_wifi_scan_get_ap_num(&ap_count);
                
                if (ap_count > 0) {
                    wifi_ap_record_t *ap_list = malloc(ap_count * sizeof(wifi_ap_record_t));
                    if (ap_list) {
                        esp_wifi_scan_get_ap_records(&ap_count, ap_list);
                        
                        /* Find strongest signal on this channel */
                        int8_t max_rssi = -100;
                        for (uint16_t i = 0; i < ap_count; i++) {
                            if (ap_list[i].primary == ch && ap_list[i].rssi > max_rssi) {
                                max_rssi = ap_list[i].rssi;
                            }
                        }
                        screen->channel_rssi[ch - 1] = max_rssi;
                        free(ap_list);
                    }
                } else {
                    screen->channel_rssi[ch - 1] = -100;  /* No signal */
                }
            } else {
                screen->channel_rssi[ch - 1] = -100;
            }
            
            vTaskDelay(pdMS_TO_TICKS(10));  /* Small delay between channels */
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  /* Delay before next full scan cycle */
    }

    ESP_LOGI(TAG, "WiFi scan task stopped");
    vTaskDelete(NULL);
}

void wlan_spectrum_screen_init(wlan_spectrum_screen_t *screen) {
    if (!screen) return;
    
    memset(screen, 0, sizeof(*screen));
    screen->num_channels = 13;  /* Support channels 1-13 (2.4 GHz) */
    
    /* Initialize all channels to -100 dBm (no signal) */
    for (uint8_t i = 0; i < MAX_WIFI_CHANNELS; i++) {
        screen->channel_rssi[i] = -100;
    }
    
    s_active_screen = screen;
    
    ESP_LOGI(TAG, "WLAN spectrum analyzer screen initialized");
}

void wlan_spectrum_screen_start_scan(wlan_spectrum_screen_t *screen) {
    if (s_scanning) {
        ESP_LOGW(TAG, "Scan already running");
        return;
    }
    
    /* Initialize NVS if needed (required for WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS needs erasing, reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return;
    }
    
    /* Initialize WiFi in station mode for scanning */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return;
    }
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    
    s_scanning = true;
    xTaskCreatePinnedToCore(
        wifi_scan_task,
        "wifi_scan",
        4096,
        screen,
        5,
        &s_scan_task,
        0  /* CPU0 */
    );
}

void wlan_spectrum_screen_stop_scan(void) {
    s_scanning = false;
    if (s_scan_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));  /* Wait for task to finish */
        s_scan_task = NULL;
    }
    
    /* Stop WiFi */
    esp_wifi_stop();
    esp_wifi_deinit();
}

void wlan_spectrum_screen_draw(wlan_spectrum_screen_t *scr) {
    uint16_t ACCENT = settings_get_accent_color();
    uint16_t TEXT   = settings_get_text_color();
    static bool first_draw = true;
    
    if (first_draw) {
        st7789_fill(COLOR_BG);
        st7789_draw_string(4, 8, "WiFi Spectrum (dBm)", ACCENT, COLOR_BG, 2);
        st7789_fill_rect(0, 26, 320, 1, ACCENT);
        st7789_draw_string(4, 155, "Scanning all channels...", TEXT, COLOR_BG, 1);
        first_draw = false;
    }
    
    /* Draw spectrum bars for each channel */
    uint16_t bar_width = 24;
    uint16_t bar_max_height = 100;
    uint16_t bar_start_y = 35;
    uint16_t x_offset = 2;
    
    for (uint8_t i = 0; i < scr->num_channels; i++) {
        uint16_t x = x_offset + (i * bar_width);
        int8_t rssi = scr->channel_rssi[i];
        
        /* Convert dBm to bar height: -100 dBm (min) to -30 dBm (strong) */
        int16_t normalized = rssi + 100;  /* 0 to 70 range */
        if (normalized < 0) normalized = 0;
        if (normalized > 70) normalized = 70;
        uint16_t bar_height = (normalized * bar_max_height) / 70;
        uint16_t y = bar_start_y + bar_max_height - bar_height;
        
        /* Color based on signal strength */
        uint16_t color;
        if (rssi < -80) {
            color = COLOR_WEAK;
        } else if (rssi < -60) {
            color = COLOR_GOOD;
        } else {
            color = COLOR_STRONG;
        }
        
        /* Clear old bar */
        st7789_fill_rect(x, bar_start_y, bar_width - 2, bar_max_height, COLOR_BG);
        
        /* Draw new bar */
        if (bar_height > 0) {
            st7789_fill_rect(x, y, bar_width - 2, bar_height, color);
        }
        
        /* Draw channel number */
        char ch_str[4];
        snprintf(ch_str, sizeof(ch_str), "%d", i + 1);
        uint16_t text_x = x + (bar_width / 2) - 3;
        st7789_draw_string(text_x, bar_start_y + bar_max_height + 4, ch_str, COLOR_TEXT, COLOR_BG, 1);
        
        /* Draw dBm value below */
        if (rssi > -100) {
            char dbm_str[6];
            snprintf(dbm_str, sizeof(dbm_str), "%d", rssi);
            st7789_draw_string(text_x, bar_start_y + bar_max_height + 14, dbm_str, COLOR_WEAK, COLOR_BG, 1);
        }
    }
    
    scr->frame_count++;
}

void wlan_spectrum_screen_exit(void) {
    wlan_spectrum_screen_stop_scan();
    st7789_fill(0x0000);  /* Black */
    ESP_LOGI(TAG, "Exited WLAN spectrum screen");
}