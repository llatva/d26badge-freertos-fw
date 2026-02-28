/*
 * MicroPython port configuration for D26 Badge
 *
 * MINIMAL config: Python interpreter core + badge native module.
 * No networking, no filesystem VFS (we use ESP-IDF FAT directly),
 * no threads, no Bluetooth.
 */
#ifndef MICROPY_INCLUDED_MPCONFIGPORT_H
#define MICROPY_INCLUDED_MPCONFIGPORT_H

#include <stdint.h>
#include <limits.h>

/* Ensure SSIZE_MAX is available (newlib may not define it).
 * Must be a pure arithmetic expression (no casts) so that the
 * C preprocessor can evaluate it inside #if directives.          */
#ifndef SSIZE_MAX
#define SSIZE_MAX (INT_MAX)
#endif

/* ---------- Object representation and NLR ---------- */
#define MICROPY_OBJ_REPR                    (MICROPY_OBJ_REPR_A)
#define MICROPY_NLR_SETJMP                  (1)

/* ---------- Memory ---------- */
#define MICROPY_ALLOC_PATH_MAX              (128)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT      (32)

/* ---------- Emitters ---------- */
#define MICROPY_PERSISTENT_CODE_LOAD        (0)
#define MICROPY_EMIT_XTENSAWIN              (0)
#define MICROPY_EMIT_INLINE_XTENSA          (0)

/* ---------- Compiler / optimisations ---------- */
#define MICROPY_COMP_CONST                  (1)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN    (1)
#define MICROPY_OPT_COMPUTED_GOTO           (1)
#define MICROPY_ENABLE_COMPILER             (1)

/* ---------- Internal features ---------- */
#define MICROPY_ENABLE_GC                   (1)
#define MICROPY_GC_SPLIT_HEAP               (0)
#define MICROPY_GC_SPLIT_HEAP_AUTO          (0)
#define MICROPY_STACK_CHECK                 (1)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE (256)
#define MICROPY_ERROR_REPORTING             (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_WARNINGS                    (1)
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_LONGINT_IMPL                (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_USE_INTERNAL_ERRNO          (1)
#define MICROPY_USE_INTERNAL_PRINTF         (0)
#define MICROPY_SCHEDULER_DEPTH             (0)
#define MICROPY_KBD_EXCEPTION               (0)
#define MICROPY_REPL_EVENT_DRIVEN           (0)
#define MICROPY_HELPER_REPL                 (0)

/* ---------- VFS - disabled (we use ESP-IDF FAT directly) ---------- */
#define MICROPY_READER_VFS                  (0)
#define MICROPY_VFS                         (0)
#define MICROPY_READER_POSIX                (0)

/* ---------- Builtins ---------- */
#define MICROPY_PY_BUILTINS_HELP            (0)
#define MICROPY_PY_BUILTINS_INPUT           (0)
#define MICROPY_PY_BUILTINS_STR_UNICODE     (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW      (1)
#define MICROPY_PY_BUILTINS_FROZENSET       (1)
#define MICROPY_PY_BUILTINS_SET             (1)
#define MICROPY_PY_BUILTINS_SLICE           (1)
#define MICROPY_PY_BUILTINS_PROPERTY        (1)
#define MICROPY_PY_BUILTINS_ENUMERATE       (1)
#define MICROPY_PY_BUILTINS_FILTER          (1)
#define MICROPY_PY_BUILTINS_REVERSED        (1)
#define MICROPY_PY_BUILTINS_MIN_MAX         (1)
#define MICROPY_PY_BUILTINS_COMPILE         (0)
#define MICROPY_PY_BUILTINS_EXECFILE        (0)

#define MICROPY_PY_MICROPYTHON_MEM_INFO     (1)
#define MICROPY_PY_ALL_SPECIAL_METHODS      (0)
#define MICROPY_PY_REVERSE_SPECIAL_METHODS  (0)
#define MICROPY_PY_ARRAY                    (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN       (1)
#define MICROPY_PY_COLLECTIONS              (1)
#define MICROPY_PY_COLLECTIONS_DEQUE        (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT  (1)
#define MICROPY_PY_MATH                     (1)
#define MICROPY_PY_CMATH                    (0)
#define MICROPY_PY_IO                       (0)
#define MICROPY_PY_IO_BUFFEREDWRITER        (0)
#define MICROPY_PY_STRUCT                   (1)
#define MICROPY_PY_SYS                      (1)
#define MICROPY_PY_SYS_EXIT                 (1)
#define MICROPY_PY_SYS_MAXSIZE              (1)
#define MICROPY_PY_SYS_MODULES              (1)
#define MICROPY_PY_SYS_PLATFORM             "badge-esp32s3"
#define MICROPY_PY_GC                       (1)

/* Disabled modules */
#define MICROPY_PY_THREAD                   (0)
#define MICROPY_PY_SOCKET                   (0)
#define MICROPY_PY_NETWORK                  (0)
#define MICROPY_PY_BLUETOOTH                (0)
#define MICROPY_PY_ESPNOW                   (0)
#define MICROPY_PY_OS                       (0)
#define MICROPY_PY_TIME                     (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (0)
#define MICROPY_PY_TIME_TIME_TIME_NS        (0)
#define MICROPY_PY_ERRNO                    (1)
#define MICROPY_PY_RANDOM                   (1)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC    (esp_random())
#define MICROPY_PY_SELECT                   (0)
#define MICROPY_PY_MACHINE                  (0)
#define MICROPY_PY_HASHLIB                  (0)
#define MICROPY_PY_BINASCII                 (0)
#define MICROPY_PY_JSON                     (0)
#define MICROPY_PY_RE                       (0)

/* Frozen modules - none */
#define MICROPY_MODULE_FROZEN_STR           (0)
#define MICROPY_MODULE_FROZEN_MPY           (0)
#define MICROPY_QSTR_EXTRA_POOL             mp_qstr_frozen_const_pool

/* ---------- Type definitions ---------- */
/* mp_int_t and mp_uint_t use MicroPython's default (intptr_t/uintptr_t) */
typedef long          mp_off_t;

/* ---------- Root pointers for GC ---------- */
#define MICROPY_PORT_ROOT_POINTERS          /* none */

/* ---------- Port-specific builtin modules ---------- */
extern const struct _mp_obj_module_t badge_module;

#define MICROPY_PORT_BUILTIN_MODULES \
    { MP_ROM_QSTR(MP_QSTR_badge), MP_ROM_PTR(&badge_module) },

#define MICROPY_PORT_BUILTINS               /* none extra */

#endif /* MICROPY_INCLUDED_MPCONFIGPORT_H */
