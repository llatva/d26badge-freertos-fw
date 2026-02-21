/*
 * Audio input driver for ICS-43434 MEMS microphone on I2S bus.
 *
 * Hardware: ESP32-S3 I2S interface
 * Microphone: ICS-43434 (I2S PDM format, mono, 48 kHz native)
 *
 * Usage:
 *   audio_init()                    – initialise I2S peripheral
 *   audio_read_samples()            – read PCM samples (blocking)
 *   audio_compute_fft()             – compute FFT on sample buffer
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* Audio buffer size for FFT (must be power of 2) */
#define AUDIO_SAMPLE_RATE  48000
#define AUDIO_FFT_SIZE     256       /* 256-point FFT */
#define AUDIO_FREQ_BINS    (AUDIO_FFT_SIZE / 2)  /* 128 frequency bins */

/* Frequency range: DC to Nyquist (24 kHz at 48 kHz sample rate) */
#define AUDIO_MAX_FREQ     (AUDIO_SAMPLE_RATE / 2)
#define AUDIO_BIN_WIDTH    (AUDIO_MAX_FREQ / AUDIO_FREQ_BINS)

/* Audio sample format: signed 16-bit PCM */
typedef int16_t audio_sample_t;

/* FFT magnitude spectrum (normalized 0-255) */
typedef uint8_t audio_magnitude_t;

/**
 * @brief  Initialise I2S input peripheral for microphone.
 *         Configures I2S for 48 kHz, 16-bit mono PCM input.
 */
void audio_init(void);

/**
 * @brief  Read AUDIO_FFT_SIZE samples from microphone (blocking).
 *         Returns raw PCM data in the provided buffer.
 *
 * @param  samples  Output buffer (must hold AUDIO_FFT_SIZE samples)
 * @return Number of samples read, or 0 on error
 */
size_t audio_read_samples(audio_sample_t *samples);

/**
 * @brief  Compute FFT on the provided sample buffer.
 *         Outputs magnitude spectrum (0-255) for each frequency bin.
 *         Uses in-place radix-2 Cooley-Tukey FFT.
 *
 * @param  samples    Input PCM samples (modified in-place during computation)
 * @param  magnitude  Output magnitudes, AUDIO_FREQ_BINS entries
 */
void audio_compute_fft(audio_sample_t *samples, audio_magnitude_t *magnitude);

/**
 * @brief  Get human-readable frequency label for a bin index.
 *         Examples: "100 Hz", "1 kHz", "5 kHz"
 *
 * @param  bin    Frequency bin (0 to AUDIO_FREQ_BINS-1)
 * @param  label  Output string buffer (must hold ~10 bytes)
 */
void audio_bin_to_freq(uint8_t bin, char *label);
