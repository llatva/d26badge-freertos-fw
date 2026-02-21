#include "micropython_runner.h"
#include "mp_bridge.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <string.h>

// MicroPython core includes
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"

static const char *TAG = "mp_runner";

// Task handle
static TaskHandle_t mp_task_handle = NULL;
static bool mp_initialized = false;
static bool mp_running = false;

// Task configuration
#define MP_TASK_STACK_SIZE (16 * 1024)  // 16KB stack for MicroPython task
#define MP_TASK_PRIORITY   5
#define MP_TASK_CORE       1             // Run on CPU1

// MicroPython heap configuration
#define MP_HEAP_SIZE (128 * 1024)  // 128KB heap for Python objects
static uint8_t mp_heap[MP_HEAP_SIZE] __attribute__((aligned(4)));
static bool mp_vm_initialized = false;

// Forward declarations
static void mp_task(void *pvParameters);

// Helper to get stack pointer (Xtensa-specific)
static inline void *get_sp(void) {
    void *sp;
    __asm__ volatile ("mov %0, sp" : "=r" (sp));
    return sp;
}

esp_err_t micropython_runner_init(void)
{
    if (mp_initialized) {
        ESP_LOGW(TAG, "MicroPython already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MicroPython runner");

    // Initialize bridge
    esp_err_t ret = mp_bridge_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bridge");
        return ret;
    }

    // Create MicroPython task on CPU1
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        mp_task,
        "micropython",
        MP_TASK_STACK_SIZE,
        NULL,
        MP_TASK_PRIORITY,
        &mp_task_handle,
        MP_TASK_CORE
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MicroPython task");
        mp_bridge_deinit();
        return ESP_FAIL;
    }

    mp_initialized = true;
    ESP_LOGI(TAG, "MicroPython runner initialized on CPU%d", MP_TASK_CORE);

    return ESP_OK;
}

esp_err_t micropython_runner_deinit(void)
{
    if (!mp_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing MicroPython runner");

    // Stop task if running
    if (mp_task_handle != NULL) {
        vTaskDelete(mp_task_handle);
        mp_task_handle = NULL;
    }

    // Deinitialize bridge
    mp_bridge_deinit();

    mp_initialized = false;
    mp_running = false;

    ESP_LOGI(TAG, "MicroPython runner deinitialized");
    return ESP_OK;
}

esp_err_t micropython_load_app(const char *app_path)
{
    if (!mp_initialized) {
        ESP_LOGE(TAG, "MicroPython not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!app_path) {
        ESP_LOGE(TAG, "Invalid app path");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading Python app: %s", app_path);

    // TODO: Implement app loading
    // For now, just log the request
    ESP_LOGW(TAG, "App loading not yet implemented");

    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t micropython_stop_app(void)
{
    if (!mp_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Python app");

    // TODO: Implement app stopping
    mp_running = false;

    return ESP_OK;
}

bool micropython_is_running(void)
{
    return mp_running;
}

// MicroPython task function
static void mp_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MicroPython task started on core %d", xPortGetCoreID());

    // Get stack pointer for MicroPython
    volatile uint32_t sp = (uint32_t)get_sp();
    
    // Initialize MicroPython
    ESP_LOGI(TAG, "Initializing MicroPython VM...");
    
    // Initialize stack checking
    mp_stack_set_top((void *)sp);
    mp_stack_set_limit(MP_TASK_STACK_SIZE - 1024);  // Leave 1KB margin
    
    // Initialize garbage collector
    gc_init(mp_heap, mp_heap + MP_HEAP_SIZE);
    
    // Initialize MicroPython runtime
    mp_init();
    mp_vm_initialized = true;
    
    ESP_LOGI(TAG, "MicroPython VM initialized successfully");
    ESP_LOGI(TAG, "Heap size: %d bytes", MP_HEAP_SIZE);
    
    // Test: Execute simple Python code
    const char *test_code = "print('Hello from MicroPython!')";
    ESP_LOGI(TAG, "Testing VM with: %s", test_code);
    
    // Try to execute test code
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Compile and execute
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, test_code, strlen(test_code), 0
        );
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_SINGLE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        
        nlr_pop();
        ESP_LOGI(TAG, "Test code executed successfully!");
    } else {
        // Exception occurred
        mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
        mp_obj_print_exception(&mp_plat_print, exc);
        ESP_LOGE(TAG, "Test code execution failed");
    }
    
    // Main loop - wait for app loading requests
    while (1) {
        // Check for commands from CPU0
        // Process Python execution
        // Handle app loading/stopping
        
        // For now, just idle
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Periodically run garbage collection
        gc_collect();
    }

    // Cleanup (should never reach here)
    ESP_LOGI(TAG, "MicroPython task exiting");
    mp_vm_initialized = false;
    mp_deinit();
    mp_task_handle = NULL;
    vTaskDelete(NULL);
}
