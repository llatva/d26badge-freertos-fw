/*
 * Board-specific MicroPython configuration for D26 Badge
 */
#ifndef MICROPY_INCLUDED_MPCONFIGBOARD_H
#define MICROPY_INCLUDED_MPCONFIGBOARD_H

#define MICROPY_HW_BOARD_NAME       "D26Badge"
#define MICROPY_HW_MCU_NAME         "ESP32S3"

/* Disable UART REPL - we use our own serial handling */
#define MICROPY_HW_ENABLE_UART_REPL (0)
#define MICROPY_HW_ENABLE_USBDEV    (0)

#endif /* MICROPY_INCLUDED_MPCONFIGBOARD_H */
