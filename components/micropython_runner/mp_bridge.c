#include "mp_bridge.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "mp_bridge";

// Queue handles
static QueueHandle_t display_cmd_queue = NULL;
static QueueHandle_t button_event_queue = NULL;
static QueueHandle_t led_cmd_queue = NULL;

// Semaphore handles
static SemaphoreHandle_t display_lock_sem = NULL;

// Queue sizes
#define DISPLAY_QUEUE_SIZE 10
#define BUTTON_QUEUE_SIZE  20
#define LED_QUEUE_SIZE     10

esp_err_t mp_bridge_init(void)
{
    ESP_LOGI(TAG, "Initializing MicroPython bridge");

    // Create display command queue
    display_cmd_queue = xQueueCreate(DISPLAY_QUEUE_SIZE, sizeof(mp_display_cmd_t));
    if (!display_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create display command queue");
        return ESP_ERR_NO_MEM;
    }

    // Create button event queue
    button_event_queue = xQueueCreate(BUTTON_QUEUE_SIZE, sizeof(mp_button_event_t));
    if (!button_event_queue) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        vQueueDelete(display_cmd_queue);
        return ESP_ERR_NO_MEM;
    }

    // Create LED command queue
    led_cmd_queue = xQueueCreate(LED_QUEUE_SIZE, sizeof(mp_led_cmd_t));
    if (!led_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create LED command queue");
        vQueueDelete(display_cmd_queue);
        vQueueDelete(button_event_queue);
        return ESP_ERR_NO_MEM;
    }

    // Create display lock semaphore (mutex)
    display_lock_sem = xSemaphoreCreateMutex();
    if (!display_lock_sem) {
        ESP_LOGE(TAG, "Failed to create display lock semaphore");
        vQueueDelete(display_cmd_queue);
        vQueueDelete(button_event_queue);
        vQueueDelete(led_cmd_queue);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Bridge initialized successfully");
    return ESP_OK;
}

esp_err_t mp_bridge_deinit(void)
{
    if (display_cmd_queue) {
        vQueueDelete(display_cmd_queue);
        display_cmd_queue = NULL;
    }
    if (button_event_queue) {
        vQueueDelete(button_event_queue);
        button_event_queue = NULL;
    }
    if (led_cmd_queue) {
        vQueueDelete(led_cmd_queue);
        led_cmd_queue = NULL;
    }
    if (display_lock_sem) {
        vSemaphoreDelete(display_lock_sem);
        display_lock_sem = NULL;
    }

    ESP_LOGI(TAG, "Bridge deinitialized");
    return ESP_OK;
}

esp_err_t mp_bridge_send_display_cmd(const mp_display_cmd_t *cmd, uint32_t timeout_ms)
{
    if (!display_cmd_queue || !cmd) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(display_cmd_queue, cmd, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mp_bridge_recv_display_cmd(mp_display_cmd_t *cmd, uint32_t timeout_ms)
{
    if (!display_cmd_queue || !cmd) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(display_cmd_queue, cmd, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mp_bridge_send_button_event(const mp_button_event_t *event, uint32_t timeout_ms)
{
    if (!button_event_queue || !event) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(button_event_queue, event, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mp_bridge_recv_button_event(mp_button_event_t *event, uint32_t timeout_ms)
{
    if (!button_event_queue || !event) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(button_event_queue, event, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mp_bridge_send_led_cmd(const mp_led_cmd_t *cmd, uint32_t timeout_ms)
{
    if (!led_cmd_queue || !cmd) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(led_cmd_queue, cmd, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mp_bridge_recv_led_cmd(mp_led_cmd_t *cmd, uint32_t timeout_ms)
{
    if (!led_cmd_queue || !cmd) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(led_cmd_queue, cmd, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t mp_bridge_lock_display(uint32_t timeout_ms)
{
    if (!display_lock_sem) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(display_lock_sem, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void mp_bridge_unlock_display(void)
{
    if (display_lock_sem) {
        xSemaphoreGive(display_lock_sem);
    }
}
