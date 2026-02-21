/*
 * Minimal HAL port for MicroPython on badge
 */

#ifndef MICROPY_INCLUDED_MPHALPORT_H
#define MICROPY_INCLUDED_MPHALPORT_H

#include "py/mpconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include <stdio.h>

// Default ticks period
#define MICROPY_PY_TIME_TICKS_PERIOD_MS (1ULL << 29)

// Stdout print structure
extern const mp_print_t mp_plat_print;

// Time functions
static inline mp_uint_t mp_hal_ticks_ms(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static inline mp_uint_t mp_hal_ticks_us(void) {
    return esp_timer_get_time();
}

static inline void mp_hal_delay_ms(mp_uint_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

static inline void mp_hal_delay_us(mp_uint_t us) {
    ets_delay_us(us);
}

// Basic I/O (disabled for embedded mode)
#define mp_hal_stdin_rx_chr() (0)
#define mp_hal_stdout_tx_str(s) printf("%s", s)
#define mp_hal_stdout_tx_strn(s, len) printf("%.*s", (int)(len), s)
#define mp_hal_stdout_tx_strn_cooked(s, len) mp_hal_stdout_tx_strn(s, len)

#endif // MICROPY_INCLUDED_MPHALPORT_H
