#ifndef MICROPYTHON_RUNNER_H
#define MICROPYTHON_RUNNER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MicroPython runner
 * 
 * Creates MicroPython task on CPU1 and initializes the Python VM.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t micropython_runner_init(void);

/**
 * @brief Deinitialize MicroPython runner
 * 
 * Stops the MicroPython task and cleans up resources.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t micropython_runner_deinit(void);

/**
 * @brief Load and run a Python app from filesystem
 * 
 * @param app_path Path to the Python app directory (e.g., "/pyapps/snake")
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t micropython_load_app(const char *app_path);

/**
 * @brief Stop currently running Python app
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t micropython_stop_app(void);

/**
 * @brief Check if MicroPython is running
 * 
 * @return true if MicroPython task is running, false otherwise
 */
bool micropython_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // MICROPYTHON_RUNNER_H
