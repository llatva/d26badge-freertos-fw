#ifndef MP_BRIDGE_H
#define MP_BRIDGE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Display command types
typedef enum {
    MP_DISP_CMD_CLEAR,
    MP_DISP_CMD_PIXEL,
    MP_DISP_CMD_TEXT,
    MP_DISP_CMD_RECT,
    MP_DISP_CMD_SHOW,
} mp_display_cmd_type_t;

// Display command structure
typedef struct {
    mp_display_cmd_type_t type;
    union {
        struct { uint16_t color; } clear;
        struct { int16_t x, y; uint16_t color; } pixel;
        struct { int16_t x, y; uint16_t color; char text[64]; } text;
        struct { int16_t x, y, w, h; uint16_t color; bool fill; } rect;
    } params;
} mp_display_cmd_t;

// Button event structure
typedef struct {
    uint8_t button_mask;  // Bitmask of pressed buttons
    bool pressed;          // True if pressed, false if released
} mp_button_event_t;

// LED command structure
typedef struct {
    uint8_t index;  // LED index (0-7) or 0xFF for all
    uint8_t r, g, b;
} mp_led_cmd_t;

/**
 * @brief Initialize bridge queues and semaphores
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mp_bridge_init(void);

/**
 * @brief Deinitialize bridge
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mp_bridge_deinit(void);

/**
 * @brief Send display command from Python to CPU0
 * 
 * @param cmd Display command to send
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if queue full
 */
esp_err_t mp_bridge_send_display_cmd(const mp_display_cmd_t *cmd, uint32_t timeout_ms);

/**
 * @brief Receive display command on CPU0
 * 
 * @param cmd Pointer to store received command
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no command available
 */
esp_err_t mp_bridge_recv_display_cmd(mp_display_cmd_t *cmd, uint32_t timeout_ms);

/**
 * @brief Send button event from CPU0 to Python
 * 
 * @param event Button event to send
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if queue full
 */
esp_err_t mp_bridge_send_button_event(const mp_button_event_t *event, uint32_t timeout_ms);

/**
 * @brief Receive button event in Python
 * 
 * @param event Pointer to store received event
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no event available
 */
esp_err_t mp_bridge_recv_button_event(mp_button_event_t *event, uint32_t timeout_ms);

/**
 * @brief Send LED command from Python to CPU0
 * 
 * @param cmd LED command to send
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if queue full
 */
esp_err_t mp_bridge_send_led_cmd(const mp_led_cmd_t *cmd, uint32_t timeout_ms);

/**
 * @brief Receive LED command on CPU0
 * 
 * @param cmd Pointer to store received command
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no command available
 */
esp_err_t mp_bridge_recv_led_cmd(mp_led_cmd_t *cmd, uint32_t timeout_ms);

/**
 * @brief Take display lock (used by both cores)
 * 
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if locked
 */
esp_err_t mp_bridge_lock_display(uint32_t timeout_ms);

/**
 * @brief Release display lock
 */
void mp_bridge_unlock_display(void);

#ifdef __cplusplus
}
#endif

#endif // MP_BRIDGE_H
