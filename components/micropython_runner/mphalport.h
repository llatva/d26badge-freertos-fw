/*
 * MicroPython HAL port header for D26 Badge
 *
 * Provides the minimal mphal interface that MicroPython's core requires.
 * NOTE: This header is included by MicroPython core files during qstr
 * preprocessing, so it must NOT include ESP-IDF headers like freertos/FreeRTOS.h.
 * ESP-IDF-dependent implementations go in mphalport.c.
 */
#ifndef MICROPY_INCLUDED_MPHALPORT_H
#define MICROPY_INCLUDED_MPHALPORT_H

#include <stdint.h>
#include <stddef.h>

/* Ticks - implemented in mphalport.c */
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);

/* Delay - implemented in mphalport.c */
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);

/* We don't use REPL but MicroPython's print() still goes through stdout_tx */
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len);
int  mp_hal_stdin_rx_chr(void);

/* Yield to allow FreeRTOS scheduler to run */
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
        mp_hal_delay_ms(1); \
    } while (0);

#define mp_hal_quiet_timing_enter() MICROPY_BEGIN_ATOMIC_SECTION()
#define mp_hal_quiet_timing_exit(irq_state) MICROPY_END_ATOMIC_SECTION(irq_state)

/* Xtensa atomic sections using RSIL/WSR */
#define MICROPY_BEGIN_ATOMIC_SECTION() ({ unsigned int _state; __asm__ __volatile__("rsil %0, 3" : "=a"(_state)); _state; })
#define MICROPY_END_ATOMIC_SECTION(state) do { __asm__ __volatile__("wsr %0, ps; isync" :: "a"(state)); } while (0)

#endif /* MICROPY_INCLUDED_MPHALPORT_H */
