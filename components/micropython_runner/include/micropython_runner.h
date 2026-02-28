#ifndef MICROPYTHON_RUNNER_H
#define MICROPYTHON_RUNNER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MicroPython runner (bridge only, no background task)
 * 
 * Sets up the bridge queues for display/LED/button communication.
 * Does NOT spawn a background task or allocate a persistent VM heap.
 * Call micropython_run_code() or micropython_run_file() to execute Python.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t micropython_runner_init(void);

/**
 * @brief Deinitialize MicroPython runner
 * 
 * Stops any background task and cleans up resources.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t micropython_runner_deinit(void);

/**
 * @brief Run a Python code string synchronously on the calling task
 * 
 * Allocates a temporary VM heap, initialises MicroPython, executes the
 * code, and tears everything down.  The calling task must have at least
 * 16 KB of stack space.
 * 
 * @param code  Null-terminated Python source code
 * @return 0 on success, -1 on error (exception printed to stdout)
 */
int micropython_run_code(const char *code);

/**
 * @brief Run a Python file synchronously on the calling task
 * 
 * Reads the file, allocates a temporary VM heap, executes the code, and
 * tears everything down.
 * 
 * @param path  Path to a .py file (e.g. "/pyapps/apps/demo.py")
 * @return 0 on success, -1 on error
 */
int micropython_run_file(const char *path);

/**
 * @brief Load and run a Python app from filesystem (background task mode)
 * 
 * Requires the background task to be running (not started by default).
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
