#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ── Button identifiers ─────────────────────────────────────────────────── */
typedef enum {
    BTN_UP     = 0,
    BTN_DOWN   = 1,
    BTN_LEFT   = 2,
    BTN_RIGHT  = 3,
    BTN_STICK  = 4,   /* joystick press */
    BTN_A      = 5,
    BTN_B      = 6,
    BTN_START  = 7,
    BTN_SELECT = 8,
    BTN_COUNT
} btn_id_t;

/* ── Event type ──────────────────────────────────────────────────────────── */
typedef enum {
    BTN_PRESSED,
    BTN_RELEASED,
} btn_event_type_t;

typedef struct {
    btn_id_t        id;
    btn_event_type_t type;
} btn_event_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise all GPIO pins and install ISR service.
 *         @p event_queue must be a FreeRTOS queue for btn_event_t items
 *         already created by the caller.
 */
void buttons_init(QueueHandle_t event_queue);

/**
 * @brief  Return true if button @p id is currently pressed (active-low).
 */
bool buttons_is_pressed(btn_id_t id);
