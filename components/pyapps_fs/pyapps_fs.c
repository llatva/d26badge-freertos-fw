/**
 * @file pyapps_fs.c
 * @brief Python applications filesystem implementation
 */

#include "pyapps_fs.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "wear_levelling.h"
#include <sys/stat.h>

static const char *TAG = "pyapps_fs";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_mounted = false;

esp_err_t pyapps_fs_init(void)
{
    if (s_mounted) {
        ESP_LOGW(TAG, "Filesystem already mounted");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Python apps filesystem");
    
    // Find the partition
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,
        PYAPPS_PARTITION_LABEL
    );
    
    if (partition == NULL) {
        ESP_LOGE(TAG, "Failed to find partition '%s'", PYAPPS_PARTITION_LABEL);
        ESP_LOGE(TAG, "Check that partition table includes a 'data/fat' partition labeled '%s'", 
                 PYAPPS_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Found partition '%s' at offset 0x%lx, size %lu KB",
             partition->label, partition->address, partition->size / 1024);
    
    // Mount configuration
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 20,  // Support up to 20 open files (Python apps + assets)
        .format_if_mount_failed = true,  // Auto-format if corrupted
        .allocation_unit_size = 4096,  // 4KB clusters for better performance
        .disk_status_check_enable = false  // Disable periodic check (saves CPU)
    };
    
    // Mount with wear levelling
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        PYAPPS_MOUNT_POINT, 
        PYAPPS_PARTITION_LABEL,
        &mount_config, 
        &s_wl_handle
    );
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(err));
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Possible causes:");
            ESP_LOGE(TAG, "  - Partition may be corrupted");
            ESP_LOGE(TAG, "  - format_if_mount_failed may have failed");
            ESP_LOGE(TAG, "Try erasing the partition manually");
        }
        return err;
    }
    
    s_mounted = true;
    ESP_LOGI(TAG, "✓ Filesystem mounted successfully at %s", PYAPPS_MOUNT_POINT);
    
    // Create default directory structure
    struct stat st;
    if (stat(PYAPPS_MOUNT_POINT "/apps", &st) != 0) {
        ESP_LOGI(TAG, "Creating /apps directory");
        mkdir(PYAPPS_MOUNT_POINT "/apps", 0755);
    }
    
    // Log filesystem stats
    uint64_t total, used, free;
    if (pyapps_fs_get_stats(&total, &used, &free) == ESP_OK) {
        ESP_LOGI(TAG, "Filesystem: %llu KB total, %llu KB used, %llu KB free",
                 total / 1024, used / 1024, free / 1024);
    }
    
    return ESP_OK;
}

void pyapps_fs_deinit(void)
{
    if (!s_mounted) {
        return;
    }
    
    if (s_wl_handle != WL_INVALID_HANDLE) {
        ESP_LOGI(TAG, "Unmounting filesystem");
        esp_vfs_fat_spiflash_unmount_rw_wl(PYAPPS_MOUNT_POINT, s_wl_handle);
        s_wl_handle = WL_INVALID_HANDLE;
        s_mounted = false;
    }
}

bool pyapps_fs_is_mounted(void)
{
    return s_mounted;
}

esp_err_t pyapps_fs_get_stats(uint64_t *total_bytes, uint64_t *used_bytes, uint64_t *free_bytes)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Get filesystem statistics using esp_vfs_fat_info
    uint64_t total = 0, free = 0;
    esp_err_t err = esp_vfs_fat_info(PYAPPS_MOUNT_POINT, &total, &free);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get filesystem stats: %s", esp_err_to_name(err));
        return err;
    }
    
    if (total_bytes) {
        *total_bytes = total;
    }
    if (free_bytes) {
        *free_bytes = free;
    }
    if (used_bytes) {
        *used_bytes = total - free;
    }
    
    return ESP_OK;
}
