/*
 * MicroPython configuration for badge runner (minimal + badge extensions)
 */

#ifndef MICROPY_INCLUDED_MPCONFIGPORT_H
#define MICROPY_INCLUDED_MPCONFIGPORT_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"

// ---- Object representation and NLR ----
#define MICROPY_OBJ_REPR                    (MICROPY_OBJ_REPR_A)
#define MICROPY_NLR_SETJMP                  (1)

// ---- Memory configuration ----
#define MICROPY_ALLOC_PATH_MAX              (128)
#define MICROPY_GC_INITIAL_HEAP_SIZE        (128 * 1024)  // 128KB heap for Python

// ---- Emitters ----
#define MICROPY_PERSISTENT_CODE_LOAD        (1)
#define MICROPY_EMIT_XTENSAWIN              (1)

// ---- Optimizations ----
#define MICROPY_OPT_COMPUTED_GOTO           (1)

// ---- Python internal features ----
#define MICROPY_READER_VFS                  (1)
#define MICROPY_ENABLE_GC                   (1)
#define MICROPY_STACK_CHECK_MARGIN          (512)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_LONGINT_IMPL                (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_ERROR_REPORTING             (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_WARNINGS                    (0)
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_STREAMS_POSIX_API           (0)
#define MICROPY_USE_INTERNAL_ERRNO          (1)
#define MICROPY_USE_INTERNAL_PRINTF         (0)  // ESP32 SDK requires its own printf
#define MICROPY_SCHEDULER_DEPTH             (4)
#define MICROPY_SCHEDULER_STATIC_NODES      (1)
#define MICROPY_VFS                         (1)

// ---- Control over Python builtins ----
#define MICROPY_PY_BUILTINS_HELP            (0)
#define MICROPY_PY_BUILTINS_HELP_TEXT       ("No help available")
#define MICROPY_PY_IO_BUFFEREDWRITER        (0)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (0)
#define MICROPY_PY_TIME_TIME_TIME_NS        (0)
#define MICROPY_PY_THREAD                   (0)  // Disable threading for simplicity

// ---- Extended modules (minimal set) ----
#define MICROPY_PY_ESPNOW                   (0)
#define MICROPY_PY_BLUETOOTH                (0)
#define MICROPY_PY_NETWORK                  (0)
#define MICROPY_PY_SOCKET                   (0)

// ---- Badge-specific configuration ----
#define MICROPY_PY_SYS_PLATFORM             "esp32s3-badge"
#define MICROPY_HW_BOARD_NAME               "D26Badge"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3"

// ---- Port-specific definitions ----
#define MICROPY_PORT_ROOT_POINTERS \
    const char *readline_hist[8]; \
    void *machine_pin_irq_handler[40];

// Type definitions - use MicroPython's defaults (intptr_t based)
// These will be defined by mpconfig.h, we just need to not conflict

// Random number generator
#define MICROPY_HW_ENABLE_RNG               (1)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC    (esp_random())

// OS-specific definitions
#define MICROPY_MIN_USE_STDOUT              (1)
#define MICROPY_HAL_HAS_VT100               (0)

// Memory allocation
extern void *mp_task_heap;
#define MICROPY_GC_SPLIT_HEAP               (0)
#define MICROPY_GC_SPLIT_HEAP_AUTO          (0)

// Extra builtins to add
#define MICROPY_PORT_BUILTINS \
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mp_builtin_open_obj) },

// Extra constants to add (badge module will be added separately)
#define MICROPY_PORT_CONSTANTS \
    { MP_ROM_QSTR(MP_QSTR_badge), MP_ROM_PTR(&badge_module) },

// Forward declarations
extern const struct _mp_obj_module_t badge_module;

#endif // MICROPY_INCLUDED_MPCONFIGPORT_H
