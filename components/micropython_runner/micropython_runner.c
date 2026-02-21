#include "micropython_runner.h"
#include "mp_bridge.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "mp_runner";

// Task handle
static TaskHandle_t mp_task_handle = NULL;
static bool mp_initialized = false;
static bool mp_running = false;

// Task configuration
#define MP_TASK_STACK_SIZE (16 * 1024)  // 16KB stack for MicroPython task
#define MP_TASK_PRIORITY   5
#define MP_TASK_CORE       1             // Run on CPU1

// Forward declarations
static void mp_task(void *pvParameters);

esp_err_t micropython_runner_init(void)
{
    if (mp_initialized) {
        ESP_LOGW(TAG, "MicroPython already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MicroPython runner");

    // Initialize bridge
    esp_err_t ret = mp_bridge_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bridge");
        return ret;
    }

    // Create MicroPython task on CPU1
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        mp_task,
        "micropython",
        MP_TASK_STACK_SIZE,
        NULL,
        MP_TASK_PRIORITY,
        &mp_task_handle,
        MP_TASK_CORE
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MicroPython task");
        mp_bridge_deinit();
        return ESP_FAIL;
    }

    mp_initialized = true;
    ESP_LOGI(TAG, "MicroPython runner initialized on CPU%d", MP_TASK_CORE);

    return ESP_OK;
}

esp_err_t micropython_runner_deinit(void)
{
    if (!mp_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing MicroPython runner");

    // Stop task if running
    if (mp_task_handle != NULL) {
        vTaskDelete(mp_task_handle);
        mp_task_handle = NULL;
    }

    // Deinitialize bridge
    mp_bridge_deinit();

    mp_initialized = false;
    mp_running = false;

    ESP_LOGI(TAG, "MicroPython runner deinitialized");
    return ESP_OK;
}

esp_err_t micropython_load_app(const char *app_path)
{
    if (!mp_initialized) {
        ESP_LOGE(TAG, "MicroPython not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!app_path) {
        ESP_LOGE(TAG, "Invalid app path");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading Python app: %s", app_path);

    // TODO: Implement app loading
    // For now, just log the request
    ESP_LOGW(TAG, "App loading not yet implemented");

    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t micropython_stop_app(void)
{
    if (!mp_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Python app");

    // TODO: Implement app stopping
    mp_running = false;

    return ESP_OK;
}

bool micropython_is_running(void)
{
    return mp_running;
}

// MicroPython task function
static void mp_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MicroPython task started on core %d", xPortGetCoreID());

    // TODO: Initialize MicroPython VM
    // For now, just keep the task alive
    while (1) {
        // Check for commands from CPU0
        // Process Python execution
        // Handle REPL if enabled

        // For now, just idle
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Task should never reach here
    ESP_LOGE(TAG, "MicroPython task exiting unexpectedly");
    mp_task_handle = NULL;
    vTaskDelete(NULL);
}
