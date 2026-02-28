/*
 * Audio spectrum screen implementation
 */

#include "audio_spectrum_screen.h"
#include "audio.h"
#include "st7789.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

#define TAG "audio_spectrum"

/* Screen layout */
#define SPECTRUM_X      4
#define SPECTRUM_Y      50
#define SPECTRUM_W      312     /* 320 - 2*4 px margins */
#define SPECTRUM_H      110     /* Height of spectrum area */
#define BAR_WIDTH       3       /* Width of each frequency bar */
#define BAR_SPACING     0       /* Gap between bars */
#define MAX_DISPLAY_BINS 107    /* Display up to 20 kHz (20000/187.5 ≈ 107) */

/* Colors */
#define COLOR_BG        0x0000  /* Black */
#define COLOR_TEXT      0xFFFF  /* White */
#define COLOR_GRID      0x4208  /* Dark gray */
#define COLOR_SPECTRUM  0x07E0  /* Green */

/* Task control */
static TaskHandle_t s_audio_task = NULL;
static bool s_audio_task_running = false;

void audio_spectrum_screen_init(audio_spectrum_screen_t *screen) {
    memset(screen, 0, sizeof(*screen));
    /* audio_init() moved to app_main */
}

void audio_spectrum_screen_update(audio_spectrum_screen_t *screen, uint8_t *new_spectrum) {
    /* Update spectrum and peak hold */
    for (int i = 0; i < AUDIO_FREQ_BINS; i++) {
        screen->spectrum[i] = new_spectrum[i];
        
        /* Peak hold: decay slowly */
        if (new_spectrum[i] > screen->peak_hold[i]) {
            screen->peak_hold[i] = new_spectrum[i];
        } else if (screen->peak_hold[i] > 0) {
            screen->peak_hold[i]--;  /* Decay by 1 per frame */
        }

        /* Max hold: accumulate maximum (persistent until cleared) */
        if (new_spectrum[i] > screen->max_hold[i]) {
            screen->max_hold[i] = new_spectrum[i];
        }
    }
    screen->frame_count++;
}

void audio_spectrum_screen_draw(audio_spectrum_screen_t *screen) {
    static bool title_drawn = false;
    static uint8_t last_spectrum[AUDIO_FREQ_BINS] = {0};
    
    /* Draw title, frequency markers, and indicators once */
    if (!title_drawn) {
        st7789_fill_rect(0, 0, 320, SPECTRUM_Y - 5, COLOR_BG);
        st7789_draw_string(4, 8, "Audio Spectrum (0-20kHz)", COLOR_TEXT, COLOR_BG, 1);
        
        /* Frequency markers at top */
        st7789_draw_string(4, 20, "DC", COLOR_GRID, COLOR_BG, 1);
        st7789_draw_string(135, 20, "10k", COLOR_GRID, COLOR_BG, 1);
        st7789_draw_string(280, 20, "20k", COLOR_GRID, COLOR_BG, 1);
        
        st7789_draw_string(4, 160, "EXIT: SELECT/A     HOLD: B", COLOR_TEXT, COLOR_BG, 1);
        title_drawn = true;
    }

    /* Draw max hold status indicator (top right area) */
    uint16_t status_x = 250;
    st7789_fill_rect(status_x, 8, 70, 16, COLOR_BG);  /* Clear status area */
    
    if (screen->max_hold_enabled) {
        st7789_draw_string(status_x, 8, "HOLD", 0xF800, COLOR_BG, 1);  /* Red text */
    }

    /* Only redraw spectrum area that actually changed (up to 20 kHz) */
    for (int i = 0; i < MAX_DISPLAY_BINS && i < AUDIO_FREQ_BINS; i++) {
        uint8_t mag = screen->spectrum[i];
        uint8_t peak = screen->peak_hold[i];
        uint8_t max_mag = screen->max_hold_enabled ? screen->max_hold[i] : 0;
        uint8_t last_mag = last_spectrum[i];

        /* Skip if nothing changed */
        if (mag == last_mag && peak <= 0 && max_mag <= 0) continue;

        uint16_t bar_x = SPECTRUM_X + (i * (BAR_WIDTH + BAR_SPACING));
        
        /* Clear old bar */
        st7789_fill_rect(bar_x, SPECTRUM_Y, BAR_WIDTH, SPECTRUM_H, COLOR_BG);

        /* Draw grid line */
        if (i % 16 == 0) {
            st7789_fill_rect(bar_x, SPECTRUM_Y, 1, SPECTRUM_H, COLOR_GRID);
        }

        /* Draw max hold bar (if enabled, shown as dim blue) */
        if (screen->max_hold_enabled && max_mag > 0) {
            uint16_t max_h = (uint16_t)max_mag * SPECTRUM_H / 255;
            uint16_t max_y = SPECTRUM_Y + SPECTRUM_H - max_h;
            st7789_fill_rect(bar_x, max_y, BAR_WIDTH, max_h, 0x041F);  /* Dark blue */
        }

        /* Draw new bar (green) */
        uint16_t bar_h = (uint16_t)mag * SPECTRUM_H / 255;
        if (bar_h > 0) {
            uint16_t bar_y = SPECTRUM_Y + SPECTRUM_H - bar_h;
            st7789_fill_rect(bar_x, bar_y, BAR_WIDTH, bar_h, COLOR_SPECTRUM);
        }

        /* Draw peak indicator (brighter green) */
        if (peak > 0) {
            uint16_t peak_y = SPECTRUM_Y + SPECTRUM_H - (peak * SPECTRUM_H / 255);
            if (peak_y >= SPECTRUM_Y) {
                st7789_fill_rect(bar_x, peak_y, BAR_WIDTH, 1, 0xAFE0);
            }
        }

        last_spectrum[i] = mag;
    }

    /* Draw frequency axis labels at bottom only once */
    if (screen->frame_count == 1) {
        st7789_fill_rect(0, SPECTRUM_Y + SPECTRUM_H + 4, 320, 12, COLOR_BG);
        st7789_draw_string(4, SPECTRUM_Y + SPECTRUM_H + 5, "DC", COLOR_TEXT, COLOR_BG, 1);
        st7789_draw_string(140, SPECTRUM_Y + SPECTRUM_H + 5, "10k", COLOR_TEXT, COLOR_BG, 1);
        st7789_draw_string(290, SPECTRUM_Y + SPECTRUM_H + 5, "20k", COLOR_TEXT, COLOR_BG, 1);
    }
}

/* Background audio capture task */
static void audio_capture_task(void *arg) {
    audio_spectrum_screen_t *screen = (audio_spectrum_screen_t *)arg;
    audio_sample_t samples[AUDIO_FFT_SIZE];
    audio_magnitude_t spectrum[AUDIO_FREQ_BINS];

    ESP_LOGI(TAG, "Audio capture task started");

    while (s_audio_task_running) {
        /* Read samples from microphone */
        size_t n = audio_read_samples(samples);
        if (n != AUDIO_FFT_SIZE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* Compute FFT */
        audio_compute_fft(samples, spectrum);

        /* Update screen spectrum */
        audio_spectrum_screen_update(screen, spectrum);

        /* Yield to allow display task to render */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(TAG, "Audio capture task stopped");
    vTaskDelete(NULL);
}

void audio_spectrum_task_start(audio_spectrum_screen_t *screen) {
    if (s_audio_task != NULL) {
        ESP_LOGW(TAG, "Audio task already running");
        return;
    }

    s_audio_task_running = true;
    xTaskCreatePinnedToCore(
        audio_capture_task,
        "audio_capture",
        4096,
        screen,
        5,
        &s_audio_task,
        0  /* CPU0 */
    );
}

void audio_spectrum_task_stop(void) {
    s_audio_task_running = false;
    if (s_audio_task != NULL) {
        xTaskNotifyGive(s_audio_task);
        s_audio_task = NULL;
    }
}

void audio_spectrum_toggle_max_hold(audio_spectrum_screen_t *screen) {
    screen->max_hold_enabled = !screen->max_hold_enabled;
    
    if (!screen->max_hold_enabled) {
        /* Clear max hold data when disabled */
        memset(screen->max_hold, 0, sizeof(screen->max_hold));
    }
    
    ESP_LOGI(TAG, "Max hold %s", screen->max_hold_enabled ? "enabled" : "disabled");
}

void audio_spectrum_screen_exit(void) {
    ESP_LOGI(TAG, "Exiting audio spectrum screen");
    audio_spectrum_task_stop();
    st7789_fill(0x0000);  /* Clear display (black) */
}
