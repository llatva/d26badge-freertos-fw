/*
 * MicroPython Runner – on-demand MicroPython VM execution
 *
 * Two execution modes:
 *
 *  1. Synchronous (micropython_run_code / micropython_run_file):
 *     Initialises the VM on the calling task's stack, runs code, and
 *     tears down.  No separate FreeRTOS task is created – safe to call
 *     from any task with enough stack space (≥ 16 KB recommended).
 *
 *  2. Background task on CPU1 (micropython_runner_init):
 *     Spawns a dedicated task pinned to CPU1 that waits for apps loaded
 *     via micropython_load_app().  Currently disabled at boot to avoid
 *     a crash in the ESP-IDF TLSF allocator during early init.
 *
 * Communication with the rest of the firmware happens through the
 * mp_bridge queues (display, LED, button events).
 */

#include "micropython_runner.h"
#include "mp_bridge.h"

#include "py/cstack.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/mpstate.h"
#include "py/nlr.h"
#include "py/runtime.h"
#include "py/builtin.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "mp_runner";

/* ──── Configuration ──── */
#define MP_TASK_STACK_SIZE  (16 * 1024)   /* 16 KB FreeRTOS stack (background task) */
#define MP_TASK_PRIORITY    5
#define MP_TASK_CORE        1             /* CPU1 (background task only) */
#define MP_HEAP_SIZE        (32 * 1024)   /* 32 KB Python heap */

/* ──── State ──── */
static TaskHandle_t mp_task_handle = NULL;
static bool mp_initialized = false;
static bool mp_running = false;
static void *mp_heap = NULL;

/* Shared with modbadge.c: badge.exit() sets this */
volatile bool mp_app_exit_requested = false;

/* Path of app to launch (set by CPU0 before waking the task) */
static char mp_app_path[128] = {0};
static volatile bool mp_app_pending = false;

/* ──── Frozen-module pool (required even if empty) ──── */
const qstr_pool_t mp_qstr_frozen_const_pool = {
    NULL,       /* prev pool */
    0,          /* total_prev_len */
    0,          /* is_sorted */
    0,          /* alloc */
    0,          /* len */
    NULL,       /* hashes */
    NULL,       /* lengths */
};

/* ──── Helper: execute a Python string (VM must already be initialised) ──── */
static int run_python_string(const char *code)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, code, strlen(code), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&pt, source_name, false);
        mp_call_function_0(module_fun);
        nlr_pop();
        return 0;
    } else {
        /* Exception */
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        return -1;
    }
}

/* ──── Helper: load and execute a .py file from filesystem ──── */
static int run_python_file(const char *path)
{
    ESP_LOGI(TAG, "executing %s", path);

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 64 * 1024) {
        ESP_LOGE(TAG, "invalid file size: %ld", size);
        fclose(f);
        return -1;
    }

    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }

    fread(buf, 1, size, f);
    fclose(f);
    buf[size] = '\0';

    int rc = run_python_string(buf);
    free(buf);
    return rc;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Synchronous (on-demand) API – runs on the calling task
 * ════════════════════════════════════════════════════════════════════════════ */

int micropython_run_code(const char *code)
{
    if (!code || !*code) return -1;

    ESP_LOGI(TAG, "Running Python code on core %d (on-demand)", xPortGetCoreID());

    /* Allocate a temporary heap for this invocation */
    void *heap = heap_caps_malloc(MP_HEAP_SIZE, MALLOC_CAP_8BIT);
    if (!heap) {
        ESP_LOGE(TAG, "failed to allocate %d byte Python heap", MP_HEAP_SIZE);
        return -1;
    }

    /* Init bridge if not already done */
    mp_bridge_init();  /* idempotent – safe to call multiple times */

    /* Initialise the VM on this thread's stack */
    volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();
    mp_cstack_init_with_top((void *)sp, MP_TASK_STACK_SIZE);
    gc_init(heap, (uint8_t *)heap + MP_HEAP_SIZE);
    mp_init();

    ESP_LOGI(TAG, "MicroPython VM initialised (heap %d KB)", MP_HEAP_SIZE / 1024);

    /* Run the code */
    mp_app_exit_requested = false;
    int rc = run_python_string(code);

    /* Tear down */
    gc_sweep_all();
    mp_deinit();
    heap_caps_free(heap);

    ESP_LOGI(TAG, "Python code finished (rc=%d)", rc);
    return rc;
}

int micropython_run_file(const char *path)
{
    if (!path || !*path) return -1;

    ESP_LOGI(TAG, "Running Python file %s on core %d (on-demand)", path, xPortGetCoreID());

    void *heap = heap_caps_malloc(MP_HEAP_SIZE, MALLOC_CAP_8BIT);
    if (!heap) {
        ESP_LOGE(TAG, "failed to allocate %d byte Python heap", MP_HEAP_SIZE);
        return -1;
    }

    mp_bridge_init();

    volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();
    mp_cstack_init_with_top((void *)sp, MP_TASK_STACK_SIZE);
    gc_init(heap, (uint8_t *)heap + MP_HEAP_SIZE);
    mp_init();

    mp_app_exit_requested = false;
    int rc = run_python_file(path);

    gc_sweep_all();
    mp_deinit();
    heap_caps_free(heap);

    ESP_LOGI(TAG, "Python file finished (rc=%d)", rc);
    return rc;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Background-task API (CPU1) – kept for future use but NOT called at boot
 * ════════════════════════════════════════════════════════════════════════════ */

/* Forward */
static void mp_task(void *pvParameters);

esp_err_t micropython_runner_init(void)
{
    if (mp_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MicroPython runner (bridge only)");

    /* Init bridge queues */
    esp_err_t ret = mp_bridge_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bridge init failed");
        return ret;
    }

    mp_initialized = true;
    ESP_LOGI(TAG, "MicroPython runner ready (use micropython_run_code() for on-demand execution)");
    return ESP_OK;
}

esp_err_t micropython_runner_deinit(void)
{
    if (!mp_initialized) return ESP_OK;

    if (mp_task_handle) {
        vTaskDelete(mp_task_handle);
        mp_task_handle = NULL;
    }
    if (mp_heap) {
        heap_caps_free(mp_heap);
        mp_heap = NULL;
    }
    mp_bridge_deinit();
    mp_initialized = false;
    mp_running = false;
    ESP_LOGI(TAG, "deinitialized");
    return ESP_OK;
}

esp_err_t micropython_load_app(const char *app_path)
{
    if (!mp_initialized) return ESP_ERR_INVALID_STATE;
    if (!app_path) return ESP_ERR_INVALID_ARG;
    if (!mp_task_handle) {
        ESP_LOGE(TAG, "background task not running; use micropython_run_file() instead");
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(mp_app_path, sizeof(mp_app_path), "%s", app_path);
    mp_app_exit_requested = false;
    mp_app_pending = true;
    ESP_LOGI(TAG, "queued app: %s", app_path);
    return ESP_OK;
}

esp_err_t micropython_stop_app(void)
{
    mp_app_exit_requested = true;
    mp_running = false;
    return ESP_OK;
}

bool micropython_is_running(void)
{
    return mp_running;
}

/* ──── The MicroPython background task (CPU1) ──── */
static void mp_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MicroPython task started on core %d", xPortGetCoreID());

    /* Get the top of this task's stack */
    volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();

    for (;;) {
        /* ── Initialise / soft-reset the VM ── */
        mp_cstack_init_with_top((void *)sp, MP_TASK_STACK_SIZE);
        gc_init(mp_heap, (uint8_t *)mp_heap + MP_HEAP_SIZE);
        mp_init();

        ESP_LOGI(TAG, "MicroPython VM initialised (heap %d KB)", MP_HEAP_SIZE / 1024);

        /* Quick self-test */
        run_python_string("print('MicroPython on badge CPU1 – OK')");

        /* ── Main loop: wait for apps from CPU0 ── */
        while (!mp_app_exit_requested) {
            if (mp_app_pending) {
                mp_app_pending = false;
                mp_running = true;
                ESP_LOGI(TAG, "launching app: %s", mp_app_path);

                int rc = run_python_file(mp_app_path);
                if (rc != 0) {
                    ESP_LOGW(TAG, "app exited with error");
                }

                mp_running = false;
                mp_app_exit_requested = false;
                ESP_LOGI(TAG, "app finished, returning to idle");
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        /* ── Soft-reset / cleanup ── */
        ESP_LOGI(TAG, "VM soft-reset");
        gc_sweep_all();
        mp_deinit();
        mp_app_exit_requested = false;
    }

    vTaskDelete(NULL);
}
