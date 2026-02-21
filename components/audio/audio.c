/*
 * Audio input driver implementation – I2S microphone (ICS-43434)
 */

#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define TAG "audio"

/* I2S port configuration */
#define AUDIO_I2S_PORT   I2S_NUM_0
#define AUDIO_I2S_SCK    GPIO_NUM_3      /* Serial clock */
#define AUDIO_I2S_WS     GPIO_NUM_8      /* Word select (LRCLK) */
#define AUDIO_I2S_DIN    GPIO_NUM_46     /* Data in (microphone output) */
#define AUDIO_I2S_DOUT   GPIO_NUM_48     /* Data out (unused for mic input) */

/* Module state */
static i2s_chan_handle_t s_rx_handle = NULL;

/**
 * @brief  Initialise I2S input for ICS-43434 microphone.
 *         Configures for 48 kHz, 16-bit PCM, mono.
 */
void audio_init(void) {
    if (s_rx_handle != NULL) {
        ESP_LOGW(TAG, "audio_init already called");
        return;
    }

    /* I2S channel configuration */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 1024;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return;
    }

    /* Standard I2S configuration: Philips format, 48 kHz, 16-bit, mono */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_SCK,
            .ws = AUDIO_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return;
    }

    /* Start I2S RX */
    ret = i2s_channel_enable(s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S RX: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return;
    }

    ESP_LOGI(TAG, "I2S audio input ready (48 kHz, 16-bit mono)");
}

/**
 * @brief  Read AUDIO_FFT_SIZE samples from I2S microphone.
 */
size_t audio_read_samples(audio_sample_t *samples) {
    if (s_rx_handle == NULL) {
        ESP_LOGE(TAG, "audio_read_samples: I2S not initialized");
        return 0;
    }

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(s_rx_handle, samples, AUDIO_FFT_SIZE * sizeof(audio_sample_t),
                                     &bytes_read, pdMS_TO_TICKS(500));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return 0;
    }

    return bytes_read / sizeof(audio_sample_t);
}

/**
 * @brief  In-place complex FFT (radix-2 Cooley-Tukey).
 *         Input is real-valued (imaginary parts are zero).
 *         On output, first half contains magnitude spectrum.
 */
static void fft_radix2(float *real, float *imag, int n) {
    if (n <= 1) return;

    /* Bit-reversal permutation */
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            float tmp_r = real[i], tmp_i = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = tmp_r;
            imag[j] = tmp_i;
        }
        int m = n / 2;
        while (j >= m) {
            j -= m;
            m /= 2;
        }
        j += m;
    }

    /* Cooley-Tukey FFT */
    for (int len = 2; len <= n; len *= 2) {
        float angle = -2.0f * M_PI / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < len / 2; j++) {
                float w_r = cosf(angle * j);
                float w_i = sinf(angle * j);

                int idx1 = i + j;
                int idx2 = i + j + len / 2;

                float t_r = w_r * real[idx2] - w_i * imag[idx2];
                float t_i = w_r * imag[idx2] + w_i * real[idx2];

                real[idx2] = real[idx1] - t_r;
                imag[idx2] = imag[idx1] - t_i;

                real[idx1] += t_r;
                imag[idx1] += t_i;
            }
        }
    }
}

/**
 * @brief  Compute FFT magnitude spectrum with Hann window.
 */
void audio_compute_fft(audio_sample_t *samples, audio_magnitude_t *magnitude) {
    /* Allocate working buffers */
    static float real[AUDIO_FFT_SIZE];
    static float imag[AUDIO_FFT_SIZE];

    /* Apply Hann window and convert to float */
    for (int i = 0; i < AUDIO_FFT_SIZE; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (AUDIO_FFT_SIZE - 1)));
        real[i] = (float)samples[i] * w / 32768.0f;
        imag[i] = 0.0f;
    }

    /* Compute FFT */
    fft_radix2(real, imag, AUDIO_FFT_SIZE);

    /* Extract magnitude spectrum (first half only, DC to Nyquist) */
    for (int i = 0; i < AUDIO_FREQ_BINS; i++) {
        float mag = sqrtf(real[i] * real[i] + imag[i] * imag[i]);
        
        /* Apply log scale and normalize to 0-255 */
        mag = 20.0f * log10f(mag + 1e-6f);      /* Convert to dB */
        mag = (mag + 80.0f) / 80.0f;            /* Normalize: -80dB to 0dB → 0-1 */
        mag = fmaxf(0.0f, fminf(1.0f, mag));    /* Clamp to [0, 1] */

        magnitude[i] = (uint8_t)(mag * 255.0f);
    }
}

/**
 * @brief  Convert frequency bin to human-readable label.
 */
void audio_bin_to_freq(uint8_t bin, char *label) {
    if (bin >= AUDIO_FREQ_BINS) {
        snprintf(label, 16, "OOB");
        return;
    }

    int freq_hz = (bin * AUDIO_BIN_WIDTH);

    if (freq_hz < 1000) {
        snprintf(label, 16, "%d Hz", freq_hz);
    } else {
        snprintf(label, 16, "%.1f kHz", freq_hz / 1000.0f);
    }
}
