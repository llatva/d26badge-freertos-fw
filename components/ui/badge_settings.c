/*
 * Badge settings implementation
 */

#include "badge_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

#define TAG "settings"

#define NVS_NAMESPACE "badge"
#define NVS_KEY_NICKNAME "nickname"
#define NVS_KEY_ACCENT   "accent"
#define NVS_KEY_TEXT     "text"

/* Default accent color: Green (0x07E0) */
#define DEFAULT_ACCENT_COLOR 0x07E0
#define DEFAULT_TEXT_COLOR   0xFFFF

static badge_settings_t s_settings = {0};
static nvs_handle_t s_nvs_handle = 0;

void settings_init(void) {
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs formatting, erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Open NVS handle */
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return;
    }

    /* Load nickname from NVS */
    size_t sz = BADGE_NICKNAME_LEN;
    if (nvs_get_str(s_nvs_handle, NVS_KEY_NICKNAME, s_settings.nickname, &sz) != ESP_OK) {
        strcpy(s_settings.nickname, "badge");
    }

    /* Load accent color from NVS */
    if (nvs_get_u16(s_nvs_handle, NVS_KEY_ACCENT, &s_settings.accent_color) != ESP_OK) {
        s_settings.accent_color = DEFAULT_ACCENT_COLOR;
    }

    /* Load text color from NVS */
    if (nvs_get_u16(s_nvs_handle, NVS_KEY_TEXT, &s_settings.text_color) != ESP_OK) {
        s_settings.text_color = DEFAULT_TEXT_COLOR;
    }
}

const badge_settings_t *settings_get(void) { return &s_settings; }
const char *settings_get_nickname(void) { return s_settings.nickname; }
uint16_t settings_get_accent_color(void) { return s_settings.accent_color; }
uint16_t settings_get_text_color(void) { return s_settings.text_color; }

void settings_set_nickname(const char *nickname) {
    if (!nickname) return;

    strncpy(s_settings.nickname, nickname, BADGE_NICKNAME_LEN-1);
    s_settings.nickname[BADGE_NICKNAME_LEN-1] = '\0';

    /* Save to NVS */
    esp_err_t ret = nvs_set_str(s_nvs_handle, NVS_KEY_NICKNAME, s_settings.nickname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save nickname: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_commit(s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Nickname saved: %s", s_settings.nickname);
    }
}

void settings_set_accent_color(uint16_t color) {
    s_settings.accent_color = color;
    if (s_nvs_handle) {
        nvs_set_u16(s_nvs_handle, NVS_KEY_ACCENT, color);
        nvs_commit(s_nvs_handle);
        ESP_LOGI(TAG, "Accent color saved: 0x%04X", color);
    }
}

void settings_set_text_color(uint16_t color) {
    s_settings.text_color = color;
    if (s_nvs_handle) {
        nvs_set_u16(s_nvs_handle, NVS_KEY_TEXT, color);
        nvs_commit(s_nvs_handle);
        ESP_LOGI(TAG, "Text color saved: 0x%04X", color);
    }
}
