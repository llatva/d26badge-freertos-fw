/*
 * Button driver for Disobey Badge 2025.
 *
 * All buttons use GPIO interrupts (edge-triggered) with a 20 ms debounce
 * timer to filter bounce.  Debounce is implemented by re-reading the pin
 * 20 ms after the initial edge and only posting an event if the level is
 * stable.
 *
 * Pin mapping (HARDWARE.md):
 *   UP=11  DOWN=1  LEFT=21  RIGHT=2  STICK=14
 *   A=13   B=38    START=12 SELECT=45
 *
 * Pull mode:
 *   All buttons except SELECT use PULL_UP (active-low).
 *   SELECT uses PULL_DOWN (active-high).
 */

#include "buttons.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define TAG "buttons"
#define DEBOUNCE_MS  20

/* ── Pin / polarity table ───────────────────────────────────────────────── */
typedef struct {
    gpio_num_t   pin;
    bool         active_low;  /* true = pressed when GPIO reads 0 */
} btn_hw_t;

static const btn_hw_t s_hw[BTN_COUNT] = {
    [BTN_UP]     = { GPIO_NUM_11,  true  },
    [BTN_DOWN]   = { GPIO_NUM_1,   true  },
    [BTN_LEFT]   = { GPIO_NUM_21,  true  },
    [BTN_RIGHT]  = { GPIO_NUM_2,   true  },
    [BTN_STICK]  = { GPIO_NUM_14,  true  },
    [BTN_A]      = { GPIO_NUM_13,  true  },
    [BTN_B]      = { GPIO_NUM_38,  true  },
    [BTN_START]  = { GPIO_NUM_12,  true  },
    [BTN_SELECT] = { GPIO_NUM_45,  false }, /* active-high (PULL_DOWN) */
};

/* ── Module state ───────────────────────────────────────────────────────── */
static QueueHandle_t s_queue;
static TimerHandle_t s_timer[BTN_COUNT];
static bool          s_last_state[BTN_COUNT];

/* ── Helpers ────────────────────────────────────────────────────────────── */
static bool read_pressed(btn_id_t id) {
    int level = gpio_get_level(s_hw[id].pin);
    return s_hw[id].active_low ? (level == 0) : (level == 1);
}

/* ── Debounce timer callback ─────────────────────────────────────────────── */
static void debounce_cb(TimerHandle_t t) {
    btn_id_t id = (btn_id_t)(uintptr_t)pvTimerGetTimerID(t);
    bool pressed = read_pressed(id);

    if (pressed == s_last_state[id]) return;  /* unchanged, ignore */
    s_last_state[id] = pressed;

    btn_event_t ev = {
        .id   = id,
        .type = pressed ? BTN_PRESSED : BTN_RELEASED,
    };
    xQueueSendFromISR(s_queue, &ev, NULL);
}

/* ── ISR handler ─────────────────────────────────────────────────────────── */
static void IRAM_ATTR gpio_isr(void *arg) {
    btn_id_t id = (btn_id_t)(uintptr_t)arg;
    /* Reset the one-shot debounce timer */
    BaseType_t woken = pdFALSE;
    xTimerResetFromISR(s_timer[id], &woken);
    portYIELD_FROM_ISR(woken);
}

/* ── Public init ─────────────────────────────────────────────────────────── */
void buttons_init(QueueHandle_t event_queue) {
    s_queue = event_queue;

    gpio_install_isr_service(0);

    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_hw[i].pin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = s_hw[i].active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = s_hw[i].active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type    = GPIO_INTR_ANYEDGE,
        };
        gpio_config(&cfg);

        s_last_state[i] = read_pressed((btn_id_t)i);

        /* One-shot debounce timer (20 ms) */
        s_timer[i] = xTimerCreate(
            "btn_debounce",
            pdMS_TO_TICKS(DEBOUNCE_MS),
            pdFALSE,                          /* one-shot */
            (void *)(uintptr_t)i,             /* ID = button index */
            debounce_cb
        );
        configASSERT(s_timer[i]);

        gpio_isr_handler_add(s_hw[i].pin, gpio_isr, (void *)(uintptr_t)i);
    }

    ESP_LOGI(TAG, "Buttons ready (%d inputs)", BTN_COUNT);
}

bool buttons_is_pressed(btn_id_t id) {
    return read_pressed(id);
}
