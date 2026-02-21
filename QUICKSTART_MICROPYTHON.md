# MicroPython Integration - Quick Start Guide

This guide helps you get started with the MicroPython integration immediately.

## Phase 1: Partition Table Setup (Start Here!)

### Step 1: Update Configuration

Edit `sdkconfig.defaults` and add:

```ini
# Use custom partition table with OTA + Python apps support
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_ota_micropython.csv"

# Enable FAT filesystem for Python apps
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
```

### Step 2: Clean & Rebuild

```bash
cd /home/llatva/git/d26badge-freertos-fw

# Clean previous build
make clean

# Rebuild with new partition table
make build
```

### Step 3: Flash (WARNING: Erases all data!)

```bash
# Erase flash (required for partition table change)
idf.py -p /dev/ttyUSB0 erase-flash

# Flash new firmware
make flash PORT=/dev/ttyUSB0

# Open monitor
make monitor PORT=/dev/ttyUSB0
```

## Phase 2: Add Filesystem Support

### Create FAT Mount Component

Create `components/pyapps_fs/`:

```bash
mkdir -p components/pyapps_fs
```

**File:** `components/pyapps_fs/CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "pyapps_fs.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs wear_levelling
)
```

**File:** `components/pyapps_fs/include/pyapps_fs.h`
```c
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PYAPPS_MOUNT_POINT "/pyapps"

/**
 * Initialize and mount the Python apps FAT partition
 * Returns ESP_OK on success
 */
esp_err_t pyapps_fs_init(void);

/**
 * Unmount and cleanup
 */
void pyapps_fs_deinit(void);

#ifdef __cplusplus
}
#endif
```

**File:** `components/pyapps_fs/pyapps_fs.c`
```c
#include "pyapps_fs.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "wear_levelling.h"

static const char *TAG = "pyapps_fs";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

esp_err_t pyapps_fs_init(void)
{
    ESP_LOGI(TAG, "Mounting Python apps filesystem");
    
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 10,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        PYAPPS_MOUNT_POINT, 
        "pyapps",  // Partition label
        &mount_config, 
        &s_wl_handle
    );
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Filesystem mounted at %s", PYAPPS_MOUNT_POINT);
    return ESP_OK;
}

void pyapps_fs_deinit(void)
{
    if (s_wl_handle != WL_INVALID_HANDLE) {
        esp_vfs_fat_spiflash_unmount_rw_wl(PYAPPS_MOUNT_POINT, s_wl_handle);
        s_wl_handle = WL_INVALID_HANDLE;
        ESP_LOGI(TAG, "Filesystem unmounted");
    }
}
```

### Integrate into main.c

Add to `main/main.c`:

```c
#include "pyapps_fs.h"

void app_main(void)
{
    // ... existing initialization ...
    
    // Mount Python apps filesystem
    esp_err_t ret = pyapps_fs_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Python apps filesystem not available");
    } else {
        ESP_LOGI(TAG, "Python apps ready");
    }
    
    // ... rest of initialization ...
}
```

## Phase 3: Test Filesystem

### Create Test App

Once flashed, connect serial and test:

```bash
# In FreeRTOS menu, add a test command to verify mounting
# You should see: "Python apps ready" in logs
```

### Manually Test FAT Partition

Add temporary test code to `main.c`:

```c
// After pyapps_fs_init()
FILE *f = fopen("/pyapps/test.txt", "w");
if (f) {
    fprintf(f, "Hello from Python apps partition!\n");
    fclose(f);
    ESP_LOGI(TAG, "Test file written to /pyapps/");
    
    // Read back
    f = fopen("/pyapps/test.txt", "r");
    if (f) {
        char line[128];
        fgets(line, sizeof(line), f);
        ESP_LOGI(TAG, "Read: %s", line);
        fclose(f);
    }
}
```

## Next Steps

Once Phase 1-3 are working:

1. **Phase 4**: Add MicroPython submodule
2. **Phase 5**: Create `micropython_runner` component
3. **Phase 6**: Implement `badge` Python module API

See `MICROPYTHON_INTEGRATION_PLAN.md` for full details.

## Troubleshooting

### "Partition 'pyapps' not found"
- Verify partition table flashed: `esptool.py -p /dev/ttyUSB0 read_flash 0x8000 0x1000 ptable.bin`
- Check `partitions_ota_micropython.csv` is in project root
- Confirm `sdkconfig` has `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_ota_micropython.csv"`

### "Filesystem mount failed"
- Check partition label matches: "pyapps" in both `.csv` and code
- Verify partition is `data, fat` type
- Try `format_if_mount_failed = true` in mount config

### Build fails with "component not found"
- Ensure `components/pyapps_fs/` directory exists
- Check `CMakeLists.txt` syntax
- Run `idf.py reconfigure` to regenerate build files

## Quick Verification Checklist

- [ ] Partition table created: `partitions_ota_micropython.csv`
- [ ] sdkconfig updated with custom partition config
- [ ] Build succeeds: `make build`
- [ ] Flash erased and programmed: `make flash`
- [ ] Boot logs show "Python apps ready"
- [ ] FAT filesystem mounted at `/pyapps`
- [ ] Test file read/write works

---

**Status:** Phase 1 implementation ready  
**Next:** Proceed to Phase 2 (MicroPython component)
