/*
 * Disobey Badge 2025 – FreeRTOS LED Menu Firmware
 * ================================================
 *
 * Architecture overview:
 *
 *   app_main() (CPU0)
 *     ├── Initialise drivers: ST7789, SK6812, buttons
 *     ├── Spawn input_task  – reads button events from ISR queue
 *     ├── Spawn display_task – owns the SPI bus; draws menu on request
 *     └── Spawn led_task    – drives SK6812 LEDs based on active mode
 *
 *  Shared state:
 *   - g_btn_queue   : ISR → input_task (btn_event_t)
 *   - g_disp_queue  : input_task → display_task (disp_cmd_t)
 *   - g_led_mode    : atomically updated int; led_task polls it
 *
 *  CPU affinity:
 *   - All tasks are pinned to CPU0 (PRO_CPU_NUM).
 *   - CPU1 is left free for future MicroPython VM.
 *
 * LED modes (menu items):
 *   0 – All Off
 *   1 – Red Solid
 *   2 – Green Solid
 *   3 – Blue Solid
 *   4 – Rainbow Cycle
 *   5 – Badge Identity  (DISOBEY colour wheel)
 *
 */

#include <math.h>
#include <stdlib.h>
#include "st7789.h"
#include "sk6812.h"
#include "buttons.h"
#include "menu_ui.h"
#include "version.h"
#include "audio_spectrum_screen.h"
#include "text_input_screen.h"
#include "badge_settings.h"
#include "idle_screen.h"
#include "ui_test_screen.h"
#include "sensor_readout_screen.h"
#include "signal_strength_screen.h"
#include "wlan_spectrum_screen.h"
#include "ui_test_screen.h"
#include "about_screen.h"
#include "color_select_screen.h" /* New */
#include "audio.h"              /* Added for VU meter mode */
#include "micropython_runner.h"  /* MicroPython integration */
#include "pyapps_fs.h"          /* Python apps filesystem */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>
#include <stdatomic.h>
#include <stdatomic.h>

#define TAG "main"

/* ── Queue sizes ─────────────────────────────────────────────────────────── */
#define BTN_QUEUE_LEN  16
#define DISP_QUEUE_LEN  4

/* ── LED mode ─────────────────────────────────────────────────────────────── */
typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_RED,
    LED_MODE_GREEN,
    LED_MODE_BLUE,
    LED_MODE_RAINBOW,
    LED_MODE_IDENTITY,
    LED_MODE_ACCENT,    /* Breathing with user accent color (wide swing) */
    LED_MODE_DISCO,     /* Fast random colors */
    LED_MODE_POLICE,    /* Red/Blue strobe on sides */
    LED_MODE_RELAX,     /* Slow smooth color morphing */
    LED_MODE_ROTATE,    /* Color rotating around the frame */
    LED_MODE_CHASE,     /* Single lit LED chasing */
    LED_MODE_MORPH,     /* Slow morph between colors */
    LED_MODE_BREATH_CYC,/* Breathing while color cycling */
    LED_MODE_FLAME,     /* Simulated flames on sides */
    LED_MODE_VU,        /* VU meter mode (MIC ON!) */
    LED_MODE_COUNT
} led_mode_t;

/* ── Application state ──────────────────────────────────────────────────── */
typedef enum {
    APP_STATE_IDLE = 0,
    APP_STATE_MENU = 1,
    APP_STATE_AUDIO_SPECTRUM = 2,
    APP_STATE_SETTINGS = 3,
    APP_STATE_SENSOR_READOUT = 4,
    APP_STATE_SIGNAL_STRENGTH = 5,
    APP_STATE_WLAN_SPECTRUM = 6,
    APP_STATE_UI_TEST = 7,
    APP_STATE_ABOUT = 8,
    APP_STATE_COLOR_SELECT,
    APP_STATE_TEXT_COLOR_SELECT,
} app_state_t;

static atomic_int g_app_state = APP_STATE_IDLE;

/* ── Display command / Redraw helper ────────────────────────────────────── */
typedef enum {
    DISP_CMD_REDRAW_FULL,
    DISP_CMD_REDRAW_ITEM,
} disp_cmd_type_t;

typedef struct {
    disp_cmd_type_t type;
} disp_cmd_t;

static QueueHandle_t  g_disp_queue;

static inline void request_redraw(disp_cmd_type_t type) {
    disp_cmd_t c = { .type = type };
    if (g_disp_queue) {
        xQueueSend(g_disp_queue, &c, pdMS_TO_TICKS(10));
    }
}

/* ── Shared globals ──────────────────────────────────────────────────────── */
static QueueHandle_t  g_btn_queue;
static atomic_int     g_led_mode = LED_MODE_ACCENT;
static menu_t         g_menu;           /* Main menu */
static menu_t         g_tools_menu;     /* Tools submenu */
static menu_t         g_diag_menu;      /* Diagnostics submenu */
static menu_t         g_settings_menu;  /* Settings submenu */
static menu_t         g_led_menu;       /* LED Animation submenu */
static menu_t        *g_current_menu;   /* Pointer to current menu */
static audio_spectrum_screen_t g_audio_screen;
static text_input_screen_t g_text_input_screen;
static ui_test_screen_t g_ui_test_screen;
static sensor_readout_screen_t g_sensor_screen;
static signal_strength_screen_t g_signal_screen;
static wlan_spectrum_screen_t g_wlan_spectrum_screen;
static color_select_screen_t g_color_screen;  /* New color select screen */

/* ── Forward declarations ────────────────────────────────────────────────── */
static void action_led_off(void);
static void action_led_rainbow(void);
static void action_led_identity(void);
static void action_led_accent(void);   /* New action for accent color */
static void action_audio_spectrum(void);
static void action_settings(void);
static void action_ui_test(void);
static void action_sensor_readout(void);
static void action_signal_strength(void);
static void action_wlan_spectrum(void);
static void action_about(void);
static void action_placeholder(void);
static void action_color_select(void);  /* New action for color select */

/* ── Menu action callbacks ───────────────────────────────────────────────── */
static void action_led_off(void)      { atomic_store(&g_led_mode, LED_MODE_OFF);      }
static void action_led_rainbow(void)  { atomic_store(&g_led_mode, LED_MODE_RAINBOW);  }
static void action_led_identity(void) { atomic_store(&g_led_mode, LED_MODE_IDENTITY); }
static void action_led_accent(void)   { atomic_store(&g_led_mode, LED_MODE_ACCENT);    }
static void action_led_disco(void)    { atomic_store(&g_led_mode, LED_MODE_DISCO);    }
static void action_led_police(void)   { atomic_store(&g_led_mode, LED_MODE_POLICE);   }
static void action_led_relax(void)    { atomic_store(&g_led_mode, LED_MODE_RELAX);    }
static void action_led_rotate(void)   { atomic_store(&g_led_mode, LED_MODE_ROTATE);   }
static void action_led_chase(void)    { atomic_store(&g_led_mode, LED_MODE_CHASE);    }
static void action_led_morph(void)    { atomic_store(&g_led_mode, LED_MODE_MORPH);    }
static void action_led_breath_cyc(void) { atomic_store(&g_led_mode, LED_MODE_BREATH_CYC); }
static void action_led_flame(void)     { atomic_store(&g_led_mode, LED_MODE_FLAME);     }
static void action_led_vu(void)        { atomic_store(&g_led_mode, LED_MODE_VU);        }

static void action_about(void) {
    ESP_LOGI(TAG, "Launching About Screen...");
    atomic_store(&g_app_state, APP_STATE_ABOUT);
}

static void action_placeholder(void)  { ESP_LOGI(TAG, "Not yet implemented"); }

static void action_audio_spectrum(void) {
    ESP_LOGI(TAG, "Launching Audio Spectrum Analyzer...");
    atomic_store(&g_app_state, APP_STATE_AUDIO_SPECTRUM);
    audio_spectrum_screen_init(&g_audio_screen);
    audio_spectrum_task_start(&g_audio_screen);
}

static void action_settings(void) {
    ESP_LOGI(TAG, "Launching Settings – Nickname Editor...");
    atomic_store(&g_app_state, APP_STATE_SETTINGS);
    text_input_init(&g_text_input_screen, "Nickname (Max 10):", 11);
    text_input_set_text(&g_text_input_screen, settings_get_nickname());
}

static void action_ui_test(void) {
    ESP_LOGI(TAG, "Launching UI Test Screen...");
    atomic_store(&g_app_state, APP_STATE_UI_TEST);
    ui_test_screen_init(&g_ui_test_screen);
}

static void action_sensor_readout(void) {
    ESP_LOGI(TAG, "Launching Sensor Readout...");
    atomic_store(&g_app_state, APP_STATE_SENSOR_READOUT);
    sensor_readout_screen_init(&g_sensor_screen);
}

static void action_signal_strength(void) {
    ESP_LOGI(TAG, "Launching Signal Strength Display...");
    atomic_store(&g_app_state, APP_STATE_SIGNAL_STRENGTH);
    signal_strength_screen_init(&g_signal_screen);
}

static void action_wlan_spectrum(void) {
    ESP_LOGI(TAG, "Launching WLAN Spectrum Analyzer...");
    atomic_store(&g_app_state, APP_STATE_WLAN_SPECTRUM);
    wlan_spectrum_screen_init(&g_wlan_spectrum_screen);
}

static void action_color_select(void) {
    ESP_LOGI(TAG, "Launching Accent Color Selector...");
    color_select_screen_init(&g_color_screen, settings_get_accent_color(), "Accent Color");
    atomic_store(&g_app_state, APP_STATE_COLOR_SELECT);
    request_redraw(DISP_CMD_REDRAW_FULL);
}

static void action_text_color_select(void) {
    ESP_LOGI(TAG, "Launching Text Color Selector...");
    color_select_screen_init(&g_color_screen, settings_get_text_color(), "Text Color");
    atomic_store(&g_app_state, APP_STATE_TEXT_COLOR_SELECT);
    request_redraw(DISP_CMD_REDRAW_FULL);
}

/* ── Rainbow helper ──────────────────────────────────────────────────────── */
static sk6812_color_t wheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85)  return (sk6812_color_t){ 255 - pos * 3,   0,           pos * 3   };
    if (pos < 170) { pos -= 85;  return (sk6812_color_t){ 0,           pos * 3,   255 - pos * 3 }; }
    pos -= 170;    return (sk6812_color_t){ pos * 3,         255 - pos * 3, 0           };
}

/* ── LED task ────────────────────────────────────────────────────────────── */
/*
 * DISOBEY identity palette: magenta/pink, white, black.
 * 8 LEDs: 4 magenta alternating with 4 white, fading in/out.
 */
#define IDENTITY_STEPS 64
static const sk6812_color_t DISOBEY_A = {255,  0, 200};  /* hot magenta */
static const sk6812_color_t DISOBEY_B = {255,255, 255};  /* white       */

static void led_task(void *arg) {
    (void)arg;
    uint32_t phase = 0;

    while (1) {
        led_mode_t mode = (led_mode_t)atomic_load(&g_led_mode);

        switch (mode) {
        case LED_MODE_OFF:
            sk6812_clear();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_RED:
            sk6812_fill(sk6812_scale(SK6812_RED, 40));
            sk6812_show();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_GREEN:
            sk6812_fill(sk6812_scale(SK6812_GREEN, 40));
            sk6812_show();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_BLUE:
            sk6812_fill(sk6812_scale(SK6812_BLUE, 40));
            sk6812_show();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_RAINBOW:
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                sk6812_set(i, sk6812_scale(wheel((i * 32 + phase) & 0xFF), 127));
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(30));
            break;

        case LED_MODE_IDENTITY: {
            /* 
             * DISOBEY Dynamic Identity:
             * Cycles through 4 sub-animations every ~5 seconds.
             */
            uint16_t sub_phase = (uint16_t)(phase & 1023);
            uint8_t sub_mode   = (sub_phase >> 8);  /* 0, 1, 2, or 3 */
            uint8_t t8         = (uint8_t)(sub_phase & 0xFF);

            if (sub_mode == 0) {
                /* Sub-mode 0: Breathing alternating colors (classic) */
                float t = (sinf((float)t8 * 0.05f) + 1.0f) / 2.0f;
                uint8_t bri = (uint8_t)(t * 50.0f + 10.0f);
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    sk6812_color_t c = (i % 2 == 0) ? DISOBEY_A : DISOBEY_B;
                    sk6812_set(i, sk6812_scale(c, bri));
                }
            } else if (sub_mode == 1) {
                /* Sub-mode 1: Rotating Magenta block on White background */
                uint8_t p = (t8 / 16) % SK6812_LED_COUNT;
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    if (i == p) {
                        sk6812_set(i, sk6812_scale(DISOBEY_A, 60));
                    } else {
                        sk6812_set(i, sk6812_scale(DISOBEY_B, 20));
                    }
                }
            } else if (sub_mode == 2) {
                /* Sub-mode 2: Disobey Scanner (Magenta scanner) */
                int pos = (t8 / 10) % (SK6812_LED_COUNT * 2);
                if (pos >= SK6812_LED_COUNT) pos = (SK6812_LED_COUNT * 2 - 1) - pos;
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    if (i == pos) {
                        sk6812_set(i, sk6812_scale(DISOBEY_A, 60));
                    } else {
                        sk6812_set(i, sk6812_scale(DISOBEY_B, 10));
                    }
                }
            } else {
                /* Sub-mode 3: Stroboscopic Magenta/White swap (alternating sides) */
                bool flip = (t8 % 20 < 10);
                uint8_t half = SK6812_LED_COUNT / 2;
                for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                    if (i < half) {
                        sk6812_set(i, sk6812_scale(flip ? DISOBEY_A : DISOBEY_B, 50));
                    } else {
                        sk6812_set(i, sk6812_scale(flip ? DISOBEY_B : DISOBEY_A, 50));
                    }
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case LED_MODE_ACCENT: {
            /* Breathing effect with the selected accent color (wide swing) */
            uint16_t c16 = settings_get_accent_color();
            /* Convert RGB565 to RGB888 base color */
            uint8_t r5 = (c16 >> 11) & 0x1F;
            uint8_t g6 = (c16 >> 5)  & 0x3F;
            uint8_t b5 = c16 & 0x1F;
            sk6812_color_t base = {
                (uint8_t)((r5 << 3) | (r5 >> 2)),
                (uint8_t)((g6 << 2) | (g6 >> 4)),
                (uint8_t)((b5 << 3) | (b5 >> 2))
            };

            /* Sine breathing: (sin(t) + 1) / 2 */
            float s = (sinf((float)phase * 0.05f) + 1.0f) / 2.0f;
            /* Wide swing: 1 to 90 brightness (approx half of previous max) */
            uint8_t bri = (uint8_t)(s * 89.0f + 1.0f);
            
            sk6812_fill(sk6812_scale(base, bri));
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        }

        case LED_MODE_DISCO: {
            /* Rapid flashing and random colors / patterns (disco style) */
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                if (((phase >> 1) + i) % 3 == 0) {
                    sk6812_set(i, sk6812_scale(wheel((phase * 16 + i * 16) & 0xFF), 127));
                } else {
                    sk6812_set(i, SK6812_BLACK);
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(60));
            break;
        }

        case LED_MODE_POLICE: {
            /* Alternating red/blue strobes on sides (disco house look) */
            uint8_t half = SK6812_LED_COUNT / 2;
            bool left_on = (phase % 10 < 5);
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                if (i < half) {
                    sk6812_set(i, left_on ? sk6812_scale(SK6812_RED, 127)   : SK6812_BLACK);
                } else {
                    sk6812_set(i, left_on ? SK6812_BLACK : sk6812_scale(SK6812_BLUE, 127));
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(80));
            break;
        }

        case LED_MODE_RELAX: {
            /* Slow, smooth smooth pulse transitions between relaxing colors */
            float s = (sinf((float)phase * 0.02f) + 1.0f) / 2.0f;
            sk6812_color_t color1 = {100, 0, 150}; // Mauve/Purple
            sk6812_color_t color2 = {0, 150, 120}; // Teal/Green
            sk6812_color_t blend = {
                (uint8_t)(color1.r * (1.0f - s) + color2.r * s),
                (uint8_t)(color1.g * (1.0f - s) + color2.g * s),
                (uint8_t)(color1.b * (1.0f - s) + color2.b * s)
            };
            sk6812_fill(sk6812_scale(blend, 25)); // Even gentler (half of 50)
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }

        case LED_MODE_ROTATE: {
            /* Rotating block of color */
            uint8_t p = (phase / 4) % SK6812_LED_COUNT;
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                uint8_t dist = (i >= p) ? (i - p) : (SK6812_LED_COUNT + i - p);
                if (dist < 3) {
                    sk6812_set(i, sk6812_scale(wheel((phase * 4) & 0xFF), (3 - dist) * 30));
                } else {
                    sk6812_set(i, SK6812_BLACK);
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(40));
            break;
        }

        case LED_MODE_CHASE: {
            /* Single bright LED chasing with a tail */
            uint8_t pos = phase % SK6812_LED_COUNT;
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                if (i == pos) {
                    sk6812_set(i, sk6812_scale(SK6812_WHITE, 100));
                } else {
                    /* dim tail */
                    uint8_t prev = (pos + SK6812_LED_COUNT - 1) % SK6812_LED_COUNT;
                    uint8_t pprev = (pos + SK6812_LED_COUNT - 2) % SK6812_LED_COUNT;
                    if (i == prev) sk6812_set(i, sk6812_scale(SK6812_BLUE, 50));
                    else if (i == pprev) sk6812_set(i, sk6812_scale(SK6812_BLUE, 20));
                    else sk6812_set(i, SK6812_BLACK);
                }
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(60));
            break;
        }

        case LED_MODE_MORPH: {
            /* Super slow hue morph of everything at once */
            sk6812_color_t color = sk6812_scale(wheel(phase), 40);
            sk6812_fill(color);
            sk6812_show();
            if ((phase % 4) == 0) phase++; /* Slower update */
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }

        case LED_MODE_BREATH_CYC: {
            /* Breathing with color cycling */
            float s = (sinf((float)phase * 0.05f) + 1.0f) / 2.0f;
            uint8_t bri = (uint8_t)(s * 80.0f + 5.0f);
            sk6812_fill(sk6812_scale(wheel(phase * 2), bri));
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        }

        case LED_MODE_FLAME: {
            /* Better flame: bright core at bottom, flickering dimming tops, relaxed movement */
            for (uint8_t i = 0; i < SK6812_LED_COUNT; i++) {
                uint8_t v_pos = i % 6; // Assume 0 is bottom, 5 is top for each bar
                
                /* 1. Base intensity: fades out as we go up */
                float base = 1.0f - ((float)v_pos * 0.15f); // 1.0, 0.85, 0.7... 0.25
                
                /* 2. Slow 'draft' effect using sine for organic sway */
                float draft = (sinf((float)phase * 0.04f + (float)v_pos * 0.3f) + 1.0f) * 0.12f;
                
                /* 3. High-frequency flicker: becomes dominant and 'unstable' at the tips */
                float instability = (float)(rand() % 100) / 100.0f;
                float flicker_amt = instability * (0.05f + (float)v_pos * 0.12f);
                
                float intensity = base + draft - flicker_amt;
                if (intensity < 0.05f) intensity = 0.0f; // Allow tips to fully extinguish
                if (intensity > 1.0f)  intensity = 1.0f;

                uint8_t h = (uint8_t)(intensity * 100.0f);
                
                /* Color mapping: 
                   - Hot (high intensity): Orange/Yellow
                   - Cooling (mid/low): Deep Red
                   - Coolest (bottom/random): Dim Red or Off */
                sk6812_color_t fire;
                fire.r = h;
                fire.g = (h > 35) ? (h - 35) : 0; // Greener (more orange) when hotter
                fire.b = (h > 85) ? (h - 85) : 0; // Touches of white at the very core
                
                sk6812_set(i, sk6812_scale(fire, 60));
            }
            sk6812_show();
            phase++;
            vTaskDelay(pdMS_TO_TICKS(50)); // Relaxed update rate (20 FPS)
            break;
        }

        case LED_MODE_VU: {
            /* Simple VU meter visualizing mic level into two 6-LED bars */
            static audio_sample_t samples[AUDIO_FFT_SIZE]; // Move off stack
            size_t n = audio_read_samples(samples);
            if (n > 0) {
                /* Calculate RMS level (root mean square) */
                int64_t sum_sq = 0;
                for (size_t i = 0; i < n; i++) {
                    int32_t s = (int32_t)samples[i];
                    sum_sq += (int64_t)s * s;
                }
                float rms = sqrtf((float)sum_sq / (float)n);
                
                /* DEBUG: Log the RMS value periodically (integer only to save stack) */
                static uint32_t last_log = 0;
                if (phase - last_log > 50) {
                    ESP_LOGI("VU", "RMS level: %d", (int)rms);
                    last_log = phase;
                }

                /* Map level to 0-6 LEDs. */
                int level = (int)((rms - 10.0f) / 100.0f);
                if (level < 0) level = 0;
                if (level > 6) level = 6;

                sk6812_clear();
                for (int i = 0; i < level; i++) {
                    sk6812_color_t color;
                    if (i < 3)      color = SK6812_GREEN;
                    else if (i < 5) color = (sk6812_color_t){140, 100, 0}; // Orange
                    else            color = SK6812_RED;
                    
                    sk6812_set(i, sk6812_scale(color, 80));
                    sk6812_set(i + 6, sk6812_scale(color, 80));
                }
                sk6812_show();
            }
            phase++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        default:
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── Display task ────────────────────────────────────────────────────────── */
static void display_task(void *arg) {
    (void)arg;
    disp_cmd_t cmd;
    app_state_t last_state = APP_STATE_IDLE;

    /* Allow other tasks to initialize */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Initial draw: idle screen with nickname */
    idle_screen_draw(settings_get_nickname());

    while (1) {
        app_state_t state = (app_state_t)atomic_load(&g_app_state);

        if (state == APP_STATE_IDLE) {
            /* Idle mode: display nickname, respond slowly */
            if (last_state != APP_STATE_IDLE) {
                /* Transitioning to idle state - reset display state */
                idle_screen_reset();
                last_state = APP_STATE_IDLE;
            }
            /* Call draw every loop, but it will skip if time hasn't changed */
            idle_screen_draw(settings_get_nickname());
            vTaskDelay(pdMS_TO_TICKS(500));  /* Check every 500ms for time change */
        } else if (state == APP_STATE_MENU) {
            /* Menu mode: respond to queue messages */
            if (last_state != APP_STATE_MENU) {
                menu_draw(g_current_menu, true);
                last_state = APP_STATE_MENU;
            }
            if (xQueueReceive(g_disp_queue, &cmd, pdMS_TO_TICKS(30)) == pdTRUE) {
                bool full = (cmd.type == DISP_CMD_REDRAW_FULL);
                menu_draw(g_current_menu, full);
            }
            /* Queue receive already delays for 30ms if empty */
        } else if (state == APP_STATE_AUDIO_SPECTRUM) {
            /* Audio spectrum mode: continuous rendering */
            audio_spectrum_screen_draw(&g_audio_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_SETTINGS) {
            /* Settings mode: text input screen */
            text_input_draw(&g_text_input_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_UI_TEST) {
            /* UI test mode: continuous rendering */
            ui_test_screen_draw(&g_ui_test_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_SENSOR_READOUT) {
            /* Sensor readout mode: continuous rendering */
            sensor_readout_screen_draw(&g_sensor_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_SIGNAL_STRENGTH) {
            /* Signal strength mode: continuous rendering */
            signal_strength_screen_draw(&g_signal_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_WLAN_SPECTRUM) {
            /* WLAN spectrum mode: continuous rendering */
            wlan_spectrum_screen_draw(&g_wlan_spectrum_screen);
            vTaskDelay(pdMS_TO_TICKS(30));  /* ~33 FPS */
        } else if (state == APP_STATE_ABOUT) {
            /* About screen: completely static redraw only once */
            if (last_state != APP_STATE_ABOUT) {
                about_screen_draw();
                last_state = APP_STATE_ABOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (state == APP_STATE_COLOR_SELECT || state == APP_STATE_TEXT_COLOR_SELECT) {
            /* Color select: respond to queue messages */
            if (last_state != state) {
                color_select_screen_draw(&g_color_screen);
                last_state = state;
            }
            if (xQueueReceive(g_disp_queue, &cmd, pdMS_TO_TICKS(30)) == pdTRUE) {
                color_select_screen_draw(&g_color_screen);
            }
        } else {
            /* Unknown state: fallback to idle */
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── Input task ──────────────────────────────────────────────────────────── */
static void input_task(void *arg) {
    (void)arg;
    btn_event_t ev;

    while (1) {
        if (xQueueReceive(g_btn_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        if (ev.type != BTN_PRESSED) continue;

        app_state_t state = (app_state_t)atomic_load(&g_app_state);

        if (state == APP_STATE_IDLE) {
            /* Idle mode: any button enters menu */
            ESP_LOGI(TAG, "Entering menu from idle");
            atomic_store(&g_app_state, APP_STATE_MENU);
            request_redraw(DISP_CMD_REDRAW_FULL);
        } else if (state == APP_STATE_AUDIO_SPECTRUM) {
            /* In audio spectrum mode: handle button actions */
            if (ev.id == BTN_B) {
                /* B button: toggle max hold */
                audio_spectrum_toggle_max_hold(&g_audio_screen);
            } else if (ev.id == BTN_START) {
                /* START button: toggle gain adjustment mode */
                audio_spectrum_toggle_gain_mode(&g_audio_screen);
            } else if (g_audio_screen.gain_adjust_mode) {
                /* In gain adjust mode: joystick left/right to change gain */
                if (ev.id == BTN_LEFT) {
                    audio_spectrum_adjust_gain(&g_audio_screen, -1);
                } else if (ev.id == BTN_RIGHT) {
                    audio_spectrum_adjust_gain(&g_audio_screen, 1);
                } else if (ev.id == BTN_SELECT || ev.id == BTN_A) {
                    /* Exit gain mode */
                    audio_spectrum_toggle_gain_mode(&g_audio_screen);
                }
            } else if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK ||
                ev.id == BTN_UP || ev.id == BTN_DOWN) {
                /* Any other button: exit spectrum mode */
                ESP_LOGI(TAG, "Exiting audio spectrum");
                audio_spectrum_screen_exit();
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_SETTINGS) {
            /* Settings mode: text input button handling */
            text_input_handle_button(&g_text_input_screen, ev.id);
            
            /* If user pressed SELECT/A to confirm */
            if ((ev.id == BTN_A || ev.id == BTN_SELECT) && !text_input_is_editing(&g_text_input_screen)) {
                ESP_LOGI(TAG, "Settings confirmed: %s", text_input_get_text(&g_text_input_screen));
                settings_set_nickname(text_input_get_text(&g_text_input_screen));
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_UI_TEST) {
            /* UI test mode: handle button actions */
            ui_test_screen_handle_button(&g_ui_test_screen, ev.id);
            
            /* Exit on SELECT or A (when not in a mode that uses them) */
            if (ev.id == BTN_SELECT || ev.id == BTN_A) {
                if (!g_ui_test_screen.updating) {
                    ESP_LOGI(TAG, "Exiting UI test screen");
                    ui_test_screen_clear();
                    atomic_store(&g_app_state, APP_STATE_MENU);
                    request_redraw(DISP_CMD_REDRAW_FULL);
                }
            }
        } else if (state == APP_STATE_SENSOR_READOUT) {
            /* Sensor readout mode: any button exits back to menu */
            if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK ||
                ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || ev.id == BTN_RIGHT) {
                ESP_LOGI(TAG, "Exiting sensor readout");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_SIGNAL_STRENGTH) {
            /* Signal strength mode: any button exits back to menu */
            if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK ||
                ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || ev.id == BTN_RIGHT) {
                ESP_LOGI(TAG, "Exiting signal strength display");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_WLAN_SPECTRUM) {
            /* WLAN spectrum mode: handle button actions */
            wlan_spectrum_screen_handle_button(&g_wlan_spectrum_screen, ev.id);
            
            /* Exit on SELECT or A (double-press) */
            if (ev.id == BTN_SELECT || ev.id == BTN_A) {
                ESP_LOGI(TAG, "Exiting WLAN spectrum analyzer");
                wlan_spectrum_screen_exit();
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_ABOUT) {
            /* About screen mode: any button exits back to menu */
            if (ev.id == BTN_SELECT || ev.id == BTN_A || ev.id == BTN_STICK || ev.id == BTN_B ||
                ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || ev.id == BTN_RIGHT) {
                ESP_LOGI(TAG, "Exiting about screen");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            }
        } else if (state == APP_STATE_COLOR_SELECT || state == APP_STATE_TEXT_COLOR_SELECT) {
            /* Color select: handle button actions */
            color_select_screen_handle_button(&g_color_screen, ev.id);
            
            /* If user pressed B (mapped to some back behavior) */
            if (ev.id == BTN_B || ev.id == BTN_LEFT) {
                ESP_LOGI(TAG, "Exiting color selector");
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            } else if (color_select_screen_is_confirmed(&g_color_screen)) {
                /* Confirmed color selection */
                uint16_t color = color_select_screen_get_color(&g_color_screen);
                if (state == APP_STATE_COLOR_SELECT) {
                    ESP_LOGI(TAG, "Saving new accent color: 0x%04X", color);
                    settings_set_accent_color(color);
                } else {
                    ESP_LOGI(TAG, "Saving new text color: 0x%04X", color);
                    settings_set_text_color(color);
                }
                atomic_store(&g_app_state, APP_STATE_MENU);
                request_redraw(DISP_CMD_REDRAW_FULL);
            } else {
                /* Update selection display */
                request_redraw(DISP_CMD_REDRAW_ITEM);
            }
        } else {
            /* Menu mode */
            switch (ev.id) {
            case BTN_UP:
                menu_navigate_up(g_current_menu);
                request_redraw(DISP_CMD_REDRAW_ITEM);
                break;

            case BTN_DOWN:
                menu_navigate_down(g_current_menu);
                request_redraw(DISP_CMD_REDRAW_ITEM);
                break;

            case BTN_LEFT:
            case BTN_B:
                /* Back to parent menu, or exit to idle if at root */
                if (menu_back(&g_current_menu)) {
                    ESP_LOGI(TAG, "Navigated back to parent menu");
                    request_redraw(DISP_CMD_REDRAW_FULL);
                } else {
                    /* Already at root menu, exit to idle */
                    ESP_LOGI(TAG, "Exiting menu to idle screen");
                    atomic_store(&g_app_state, APP_STATE_IDLE);
                    request_redraw(DISP_CMD_REDRAW_FULL);
                }
                break;

            case BTN_A:
            case BTN_STICK:    /* joystick press also activates */
            case BTN_SELECT:
                /* Check for submenu first */
                if (menu_enter_submenu(&g_current_menu)) {
                    ESP_LOGI(TAG, "Entered submenu");
                    request_redraw(DISP_CMD_REDRAW_FULL);
                } else {
                    /* No submenu, activate action */
                    menu_select(g_current_menu);
                    request_redraw(DISP_CMD_REDRAW_ITEM);
                }
                break;

            default:
                break;
            }
        }
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */
void app_main(void) {
    ESP_LOGI(TAG, "Disobey Badge 2025/26 – FreeRTOS firmware");

    /* ── Queues ── */
    g_btn_queue  = xQueueCreate(BTN_QUEUE_LEN,  sizeof(btn_event_t));
    g_disp_queue = xQueueCreate(DISP_QUEUE_LEN, sizeof(disp_cmd_t));
    configASSERT(g_btn_queue && g_disp_queue);

    /* ── Driver init ── */
    st7789_init();
    sk6812_init();
    audio_init();       /* Microphone driver init */
    buttons_init(g_btn_queue);
    settings_init();

    /* ── Filesystem init for Python apps ── */
    esp_err_t fs_ret = pyapps_fs_init();
    if (fs_ret == ESP_OK) {
        ESP_LOGI(TAG, "Python apps filesystem mounted successfully");
    } else {
        ESP_LOGW(TAG, "Failed to mount Python apps filesystem: %s", esp_err_to_name(fs_ret));
    }

    /* ── MicroPython runner init ── */
    esp_err_t mp_ret = micropython_runner_init();
    if (mp_ret == ESP_OK) {
        ESP_LOGI(TAG, "MicroPython runner initialized on CPU1");
    } else {
        ESP_LOGW(TAG, "Failed to initialize MicroPython runner: %s", esp_err_to_name(mp_ret));
    }

    /* ── Build menus with icons and submenus ── */
    
    /* Diagnostics submenu */
    menu_init(&g_diag_menu, "Diagnostics");
    menu_add_item(&g_diag_menu, 'T', "UI Test", action_ui_test, NULL);
    menu_add_item(&g_diag_menu, 'W', "WLAN Test", action_placeholder, NULL);
    menu_add_item(&g_diag_menu, 'S', "Sensor Readout", action_sensor_readout, NULL);
    menu_add_item(&g_diag_menu, 'V', "Signal Strength", action_signal_strength, NULL);
    menu_add_item(&g_diag_menu, 'Z', "WiFi Spectrum", action_wlan_spectrum, NULL);

    /* Tools submenu */
    menu_init(&g_tools_menu, "Tools");
    menu_add_item(&g_tools_menu, '@', "Audio Spectrum", action_audio_spectrum, NULL);
    
    /* LED Animation submenu */
    menu_init(&g_led_menu, "LED Animation");
    menu_add_item(&g_led_menu, 'b', "Accent Pulse", action_led_accent, NULL);
    menu_add_item(&g_led_menu, 'r', "Rainbow", action_led_rainbow, NULL);
    menu_add_item(&g_led_menu, 'd', "Disco Party", action_led_disco, NULL);
    menu_add_item(&g_led_menu, 'p', "Police Strobe", action_led_police, NULL);
    menu_add_item(&g_led_menu, 's', "Smooth Relax", action_led_relax, NULL);
    menu_add_item(&g_led_menu, 'o', "Smooth Rotate", action_led_rotate, NULL);
    menu_add_item(&g_led_menu, 'c', "LED Chase", action_led_chase, NULL);
    menu_add_item(&g_led_menu, 'm', "Color Morph", action_led_morph, NULL);
    menu_add_item(&g_led_menu, 'y', "Breath Cycle", action_led_breath_cyc, NULL);
    menu_add_item(&g_led_menu, 'i', "Disobey Identity", action_led_identity, NULL);
    menu_add_item(&g_led_menu, 'f', "Flame", action_led_flame, NULL);     /* New flame mode */
    menu_add_item(&g_led_menu, 'v', "VU meter mode (MIC ON!)", action_led_vu, NULL);
    menu_add_item(&g_led_menu, 'x', "Off", action_led_off, NULL);

    /* Settings submenu */
    menu_init(&g_settings_menu, "Settings");
    menu_add_item(&g_settings_menu, 'n', "Edit Nickname", action_settings, NULL);
    menu_add_item(&g_settings_menu, 'c', "Accent Color", action_color_select, NULL);
    menu_add_item(&g_settings_menu, 't', "Text Color", action_text_color_select, NULL);
    menu_add_item(&g_settings_menu, 'L', "LED Animation", NULL, &g_led_menu);

    /* Main menu */
    menu_init(&g_menu, TITLE_STR);
    menu_add_item(&g_menu, '#', "Tools", NULL, &g_tools_menu);
    menu_add_item(&g_menu, 'G', "Games", action_placeholder, NULL);
    menu_add_item(&g_menu, 'O', "Settings", NULL, &g_settings_menu);
    menu_add_item(&g_menu, 'D', "Diagnostics", NULL, &g_diag_menu);
    menu_add_item(&g_menu, 'X', "Development", action_placeholder, NULL);
    menu_add_item(&g_menu, '?', "About", action_about, NULL);

    g_current_menu = &g_menu;    /* ── Tasks (all on CPU0; CPU1 reserved for MicroPython) ── */
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 5, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(input_task,   "input",   2048, NULL, 6, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(led_task,     "led",     4096, NULL, 4, NULL, PRO_CPU_NUM);

    ESP_LOGI(TAG, "All tasks launched. UP/DOWN to navigate, A/STICK/SELECT to activate.");
}
