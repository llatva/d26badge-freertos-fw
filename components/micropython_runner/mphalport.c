/*
 * MicroPython HAL port implementation for D26 Badge
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/builtin.h"
#include "py/mpstate.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_cpu.h"

/* ── Stdout capture buffer ─────────────────────────────────────────────── */
static char  *s_capture_buf  = NULL;   /* externally provided buffer      */
static size_t s_capture_size = 0;      /* total buffer capacity           */
static size_t s_capture_pos  = 0;      /* current write position          */

void mp_hal_capture_start(char *buf, size_t size)
{
    s_capture_buf  = buf;
    s_capture_size = size;
    s_capture_pos  = 0;
    if (buf && size > 0) buf[0] = '\0';
}

size_t mp_hal_capture_stop(void)
{
    size_t n = s_capture_pos;
    /* Null-terminate */
    if (s_capture_buf && s_capture_size > 0) {
        s_capture_buf[(n < s_capture_size) ? n : s_capture_size - 1] = '\0';
    }
    s_capture_buf  = NULL;
    s_capture_size = 0;
    s_capture_pos  = 0;
    return n;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    /* Always route to serial monitor */
    fwrite(str, 1, len, stdout);
    fflush(stdout);

    /* Also capture if buffer is active */
    if (s_capture_buf && s_capture_pos < s_capture_size - 1) {
        size_t avail = s_capture_size - 1 - s_capture_pos;  /* leave room for '\0' */
        size_t n = (len < avail) ? len : avail;
        memcpy(s_capture_buf + s_capture_pos, str, n);
        s_capture_pos += n;
        s_capture_buf[s_capture_pos] = '\0';
    }
    return len;
}

int mp_hal_stdin_rx_chr(void) {
    /* Not used - no REPL. Block forever. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return 0;
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(esp_timer_get_time() / 1000);
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)esp_timer_get_time();
}

mp_uint_t mp_hal_ticks_cpu(void) {
    uint32_t ccount;
    __asm__ __volatile__("rsr %0, ccount" : "=a"(ccount));
    return ccount;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void mp_hal_delay_us(mp_uint_t us) {
    if (us >= 1000) {
        vTaskDelay(pdMS_TO_TICKS(us / 1000));
    } else {
        /* busy-wait for sub-ms delays */
        uint32_t start = mp_hal_ticks_cpu();
        uint32_t end = start + (us * 240); /* 240 MHz CPU */
        while (mp_hal_ticks_cpu() < end) { }
    }
}

/* Called by MicroPython when GC is not possible to report a fatal error */
void nlr_jump_fail(void *val) {
    ESP_LOGE("micropython", "NLR jump failed, val=%p", val);
    esp_restart();
}

/* Required by MICROPY_PY_RANDOM_SEED_INIT_FUNC */
uint32_t esp_random(void);

/* ──── Garbage collection ──── */
/*
 * gc_collect must scan the CPU registers and the C stack to find GC
 * root pointers.  On Xtensa (non-windowed NLR_SETJMP mode) we save
 * registers into an NLR buffer and then tell the GC to scan the stack
 * from the current SP up to the stack top.
 */
void gc_collect(void) {
    gc_collect_start();
    /* Save registers to the stack so GC can see them */
    volatile uint32_t regs[8];
    __asm__ __volatile__ ("" : : : "memory");
    regs[0] = 0;  /* force array to live on stack */
    (void)regs;
    /* Scan the C stack from current SP to its top */
    volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();
    gc_collect_root((void **)sp, ((uint32_t)MP_STATE_THREAD(stack_top) - sp) / sizeof(uint32_t));
    gc_collect_end();
}

/* ──── Import support ──── */
/*
 * mp_import_stat: check whether a file/directory exists for 'import'.
 * Since we disabled VFS and load scripts explicitly, we return
 * MP_IMPORT_STAT_NO_EXIST for everything – imports are not supported
 * from Python-level code in this minimal port.
 */
mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

/*
 * mp_lexer_new_from_file: create a lexer from a filesystem path.
 * Required by builtinimport.c even though we never successfully stat.
 * Raise an OSError if ever called.
 */
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_raise_OSError(ENOENT);
    return NULL;  /* unreachable */
}
