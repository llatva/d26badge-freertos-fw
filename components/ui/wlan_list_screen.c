/*
 * WLAN Networks List screen – shows detected WiFi networks with SSID, channel, and RSSI
 */

#include "wlan_list_screen.h"
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

#define TAG "wlan_list"

#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */
#define COLOR_WEAK      0x4208  /* Dark gray */
#define COLOR_GOOD      0x07E0  /* Green */
#define COLOR_STRONG    0xFFE0  /* Yellow */

/* WiFi scanning state */
static TaskHandle_t s_scan_task = NULL;
static bool s_scanning = false;

/* Background WiFi scanning task */
static void wifi_scan_task(void *arg) {
    wlan_list_screen_t *screen = (wlan_list_screen_t *)arg;
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,  /* All channels */
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300
    };

    ESP_LOGI(TAG, "WiFi list scan task started");

    while (s_scanning) {
        /* Start full WiFi scan */
        esp_err_t err = esp_wifi_scan_start(&scan_config, true);  /* Blocking */
        if (err == ESP_OK) {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            
            if (ap_count > 0) {
                if (ap_count > MAX_WLAN_NETWORKS) {
                    ap_count = MAX_WLAN_NETWORKS;
                }
                
                wifi_ap_record_t *ap_list = malloc(ap_count * sizeof(wifi_ap_record_t));
                if (ap_list) {
                    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
                    
                    /* Update screen with AP list */
                    screen->num_networks = ap_count;
                    for (uint16_t i = 0; i < ap_count; i++) {
                        strncpy(screen->networks[i].ssid, (char *)ap_list[i].ssid, 32);
                        screen->networks[i].ssid[32] = '\0';
                        screen->networks[i].rssi = ap_list[i].rssi;
                        screen->networks[i].channel = ap_list[i].primary;
                        screen->networks[i].auth = ap_list[i].authmode;
                    }
                    
                    free(ap_list);
                }
            } else {
                screen->num_networks = 0;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));  /* Scan every 2 seconds */
    }

    ESP_LOGI(TAG, "WiFi list scan task stopped");
    vTaskDelete(NULL);
}

void wlan_list_screen_init(wlan_list_screen_t *screen) {
    if (!screen) return;
    
    memset(screen, 0, sizeof(*screen));
    screen->scroll_offset = 0;
    
    ESP_LOGI(TAG, "WLAN networks list screen initialized");
}

void wlan_list_screen_start_scan(wlan_list_screen_t *screen) {
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
        "wifi_list_scan",
        4096,
        screen,
        5,
        &s_scan_task,
        0  /* CPU0 */
    );
}

void wlan_list_screen_stop_scan(void) {
    s_scanning = false;
    if (s_scan_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));  /* Wait for task to finish */
        s_scan_task = NULL;
    }
    
    /* Stop WiFi */
    esp_wifi_stop();
    esp_wifi_deinit();
}

void wlan_list_screen_draw(wlan_list_screen_t *screen) {
    uint16_t ACCENT = settings_get_accent_color();
    uint16_t TEXT   = settings_get_text_color();
    
    st7789_fill(COLOR_BG);
    
    /* Title */
    st7789_draw_string(4, 4, "WiFi Networks", ACCENT, COLOR_BG, 2);
    st7789_fill_rect(0, 22, 320, 1, ACCENT);
    
    /* Status line */
    char status_str[32];
    snprintf(status_str, sizeof(status_str), "Found: %d networks", screen->num_networks);
    st7789_draw_string(4, 26, status_str, TEXT, COLOR_BG, 1);
    
    /* Display networks (max 8 visible at once) */
    uint16_t y = 40;
    uint16_t line_height = 16;
    uint8_t max_visible = 8;
    
    for (uint8_t i = screen->scroll_offset; i < screen->num_networks && i < screen->scroll_offset + max_visible; i++) {
        wlan_network_info_t *net = &screen->networks[i];
        
        /* Determine color based on signal strength */
        uint16_t color;
        if (net->rssi < -80) {
            color = COLOR_WEAK;
        } else if (net->rssi < -60) {
            color = COLOR_GOOD;
        } else {
            color = COLOR_STRONG;
        }
        
        /* Draw SSID (truncate if too long) */
        char ssid_display[24];
        strncpy(ssid_display, net->ssid, 20);
        ssid_display[20] = '\0';
        st7789_draw_string(4, y, ssid_display, color, COLOR_BG, 1);
        
        /* Draw channel */
        char ch_str[8];
        snprintf(ch_str, sizeof(ch_str), "Ch%d", net->channel);
        st7789_draw_string(170, y, ch_str, TEXT, COLOR_BG, 1);
        
        /* Draw RSSI */
        char rssi_str[10];
        snprintf(rssi_str, sizeof(rssi_str), "%ddBm", net->rssi);
        st7789_draw_string(220, y, rssi_str, color, COLOR_BG, 1);
        
        /* Draw lock icon if encrypted */
        if (net->auth != WIFI_AUTH_OPEN) {
            st7789_draw_string(280, y, "L", TEXT, COLOR_BG, 1);
        }
        
        y += line_height;
    }
    
    /* Bottom instructions */
    st7789_draw_string(4, 155, "UP/DOWN: scroll  Any: exit", TEXT, COLOR_BG, 1);
}

void wlan_list_screen_handle_button(wlan_list_screen_t *screen, int button_id) {
    if (!screen) return;
    
    uint8_t max_visible = 8;
    
    if (button_id == BTN_UP) {
        /* Scroll up */
        if (screen->scroll_offset > 0) {
            screen->scroll_offset--;
        }
    } else if (button_id == BTN_DOWN) {
        /* Scroll down */
        if (screen->scroll_offset + max_visible < screen->num_networks) {
            screen->scroll_offset++;
        }
    }
}

void wlan_list_screen_exit(void) {
    wlan_list_screen_stop_scan();
    st7789_fill(0x0000);  /* Black */
    ESP_LOGI(TAG, "Exited WLAN networks list screen");
}
