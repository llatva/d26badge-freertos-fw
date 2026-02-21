/*
 * Board-specific configuration for D26Badge
 */

#ifndef MICROPY_INCLUDED_MPCONFIGBOARD_H
#define MICROPY_INCLUDED_MPCONFIGBOARD_H

// Board identification
#define MICROPY_HW_BOARD_NAME           "D26Badge"
#define MICROPY_HW_MCU_NAME             "ESP32-S3"
#define MICROPY_PY_SYS_PLATFORM         "esp32s3-badge"

// Badge hardware configuration
#define MICROPY_HW_ENABLE_UART_REPL     (0)  // No UART REPL (embedded mode)
#define MICROPY_HW_ENABLE_USBDEV        (0)  // No USB device support
#define MICROPY_HW_ESP_USB_SERIAL_JTAG  (0)  // No USB-JTAG

// Task configuration for embedded mode
#define MICROPY_TASK_STACK_SIZE         (16 * 1024)  // 16KB stack (already allocated by runner)

// Heap configuration
#define MICROPY_GC_INITIAL_HEAP_SIZE    (128 * 1024)  // 128KB Python heap

#endif // MICROPY_INCLUDED_MPCONFIGBOARD_H
