/**
 * @file pyapps_fs.h
 * @brief Python applications filesystem management for badge
 * 
 * Mounts a FAT partition for user-uploaded MicroPython applications.
 * Uses wear levelling for flash longevity.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mount point for Python applications filesystem */
#define PYAPPS_MOUNT_POINT "/pyapps"

/** Partition label in partition table */
#define PYAPPS_PARTITION_LABEL "pyapps"

/**
 * @brief Initialize and mount the Python apps FAT partition
 * 
 * This function:
 * - Finds the 'pyapps' partition
 * - Initializes wear levelling
 * - Mounts FAT filesystem at /pyapps
 * - Formats partition if mounting fails
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pyapps_fs_init(void);

/**
 * @brief Unmount and cleanup the filesystem
 * 
 * Should be called before system shutdown/restart (not typically needed).
 */
void pyapps_fs_deinit(void);

/**
 * @brief Check if filesystem is mounted and accessible
 * 
 * @return true if mounted, false otherwise
 */
bool pyapps_fs_is_mounted(void);

/**
 * @brief Get filesystem statistics
 * 
 * @param[out] total_bytes Total partition size in bytes
 * @param[out] used_bytes  Used space in bytes
 * @param[out] free_bytes  Free space in bytes
 * 
 * @return ESP_OK on success
 */
esp_err_t pyapps_fs_get_stats(uint64_t *total_bytes, uint64_t *used_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
