/*
 * Audio Spectrum Analyzer screen – visualizes microphone input as FFT spectrum.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Screen for displaying real-time audio spectrum */
typedef struct {
    uint8_t spectrum[128];      /* Current FFT magnitude (0-255) */
    uint8_t peak_hold[128];     /* Peak hold for each frequency bin */
    uint8_t max_hold[128];      /* Max hold for each frequency bin (manual hold) */
    bool max_hold_enabled;      /* Whether max hold is active */
    uint32_t frame_count;       /* Frames rendered */
    bool updating;              /* Audio reader thread is active */
} audio_spectrum_screen_t;

/**
 * @brief  Initialise the audio spectrum screen.
 */
void audio_spectrum_screen_init(audio_spectrum_screen_t *screen);

/**
 * @brief  Update spectrum data (called by audio reader thread).
 */
void audio_spectrum_screen_update(audio_spectrum_screen_t *screen, uint8_t *new_spectrum);

/**
 * @brief  Render spectrum visualization to display.
 */
void audio_spectrum_screen_draw(audio_spectrum_screen_t *screen);

/**
 * @brief  Start background audio capture task.
 *         Returns immediately; updates 'screen' in real time.
 */
void audio_spectrum_task_start(audio_spectrum_screen_t *screen);

/**
 * @brief  Stop background audio capture task.
 */
void audio_spectrum_task_stop(void);

/**
 * @brief  Toggle max hold mode on/off.
 */
void audio_spectrum_toggle_max_hold(audio_spectrum_screen_t *screen);

/**
 * @brief  Exit audio spectrum screen and return to menu.
 *         Stops audio task and clears display.
 */
void audio_spectrum_screen_exit(void);
