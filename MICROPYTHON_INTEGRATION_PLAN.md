# MicroPython Integration Plan
## Disobey Badge 2025 - FreeRTOS + MicroPython Hybrid Firmware

**Date:** February 21, 2026  
**Status:** Planning Phase  
**Target:** ESP32-S3, 4MB Flash

---

## Executive Summary

This plan integrates MicroPython as a user-extensible mini-app platform into the existing FreeRTOS badge firmware. The hybrid architecture runs FreeRTOS on CPU0 (maintaining all existing features) and spawns a MicroPython VM on CPU1 for user-uploaded Python apps. OTA update capability is included.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  ESP32-S3 (Dual Core)                                            │
│                                                                   │
│  ┌─────────────────────────────┐  ┌──────────────────────────┐ │
│  │ CPU0 (PRO_CPU)              │  │ CPU1 (APP_CPU)           │ │
│  │                             │  │                          │ │
│  │ FreeRTOS Main Firmware      │  │ MicroPython VM Task      │ │
│  │ - ST7789 Display Driver     │  │ - Mini-app execution     │ │
│  │ - SK6812 LED Driver         │  │ - Sandboxed env          │ │
│  │ - Button Input Handler      │  │ - Python REPL            │ │
│  │ - Menu System               │◄─┼─ API Bridge             │ │
│  │ - Settings Management       │  │   (message queues)       │ │
│  │ - Audio/Sensors             │  │                          │ │
│  └─────────────────────────────┘  └──────────────────────────┘ │
│                 │                              │                 │
│                 └──────────┬───────────────────┘                 │
│                            │                                     │
│                    Hardware Peripherals                          │
│              (Display, LEDs, Buttons, SPI, I2C, etc.)           │
└─────────────────────────────────────────────────────────────────┘
```

### Communication Model

- **Inter-core messaging**: FreeRTOS queues for CPU0 ↔ CPU1 communication
- **Resource arbitration**: Semaphores/mutexes for shared hardware (display, LEDs)
- **API surface**: C functions callable from MicroPython via native modules

---

## Flash Partition Layout (OTA-Ready)

### New Partition Table: `partitions_ota_micropython.csv`

```csv
# Name,       Type, SubType, Offset,  Size,     Flags
nvs,          data, nvs,     0x9000,  0x6000,
otadata,      data, ota,     0xF000,  0x2000,
phy_init,     data, phy,     0x11000, 0x1000,
factory,      app,  factory, 0x20000, 0x180000,
ota_0,        app,  ota_0,   0x1A0000, 0x180000,
ota_1,        app,  ota_1,   0x320000, 0x180000,
pyapps,       data, fat,     0x4A0000, 0x100000,
```

### Partition Details

| Name | Type | Size | Purpose |
|------|------|------|---------|
| `nvs` | data/nvs | 24KB | Settings, WiFi credentials, preferences |
| `otadata` | data/ota | 8KB | OTA update metadata (active partition marker) |
| `phy_init` | data/phy | 4KB | RF calibration data |
| `factory` | app/factory | 1.5MB | Main FreeRTOS firmware (fallback) |
| `ota_0` | app/ota_0 | 1.5MB | OTA slot 0 (updated firmware) |
| `ota_1` | app/ota_1 | 1.5MB | OTA slot 1 (updated firmware) |
| `pyapps` | data/fat | 1MB | FAT filesystem for MicroPython apps |

**Total:** ~4.67MB (fits in 4MB flash with compression)

**Optimization Note:** If 4MB is tight, reduce OTA partitions to 1.25MB each or use compressed OTA.

---

## Implementation Phases

### Phase 1: Foundation (Week 1-2)

#### 1.1 Partition Table & Filesystem

- [ ] Create `partitions_ota_micropython.csv`
- [ ] Update `sdkconfig.defaults`:
  ```
  CONFIG_PARTITION_TABLE_CUSTOM=y
  CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_ota_micropython.csv"
  ```
- [ ] Add FAT filesystem support in `CMakeLists.txt`:
  ```cmake
  idf_component_register(
      REQUIRES fatfs wear_levelling
  )
  ```
- [ ] Mount `/pyapps` FAT partition at boot
- [ ] Add filesystem init code in `main.c`

#### 1.2 MicroPython Component Integration

- [ ] Add MicroPython as ESP-IDF component:
  ```bash
  cd components/
  git submodule add https://github.com/micropython/micropython.git
  cd micropython/ports/esp32
  git submodule update --init
  ```
- [ ] Create `components/micropython_runner/` component:
  - `micropython_task.c` - spawns MP VM on CPU1
  - `mp_bridge.c` - C↔Python API bridge
  - `CMakeLists.txt` - build configuration

#### 1.3 Basic MicroPython Task

- [ ] Implement `micropython_task()`:
  - Pin to `tskNO_AFFINITY` (runs on CPU1)
  - Initialize MicroPython heap (allocate 256KB from DRAM/PSRAM)
  - Start REPL on serial (for debugging)
  - Wait for app launch commands from CPU0

---

### Phase 2: API Bridge (Week 2-3)

#### 2.1 Native Module: `badge`

Create `components/micropython_runner/modbadge.c`:

```python
# Python API (example usage)
import badge

# Display API
badge.display.clear(color=0x0000)
badge.display.text("Hello", x=10, y=20, color=0xFFFF)
badge.display.rect(x=0, y=0, w=320, h=170, color=0xF800)
badge.display.show()  # Push framebuffer to screen

# LED API
badge.leds.set(index=0, r=255, g=0, b=0)
badge.leds.fill(r=0, g=255, b=0)
badge.leds.show()

# Button API
btn = badge.buttons.get()  # Returns dict: {'up': False, 'down': True, ...}
badge.buttons.wait()  # Blocks until any button pressed

# Settings API
color = badge.settings.get_accent_color()
badge.settings.set_accent_color(0xFF00FF)

# Control API
badge.exit()  # Return to FreeRTOS menu
```

#### 2.2 Bridge Implementation

**C Side (`mp_bridge.c`):**
- Queue `mp_display_queue`: Python → CPU0 display commands
- Queue `mp_button_queue`: CPU0 → Python button events
- Semaphore `display_lock`: Protect display access
- Functions: `mp_bridge_send_display()`, `mp_bridge_get_button()`, etc.

**FreeRTOS Side (`main.c` integration):**
- Monitor `mp_display_queue` in `display_task`
- Forward button events to `mp_button_queue` when app active
- Handle resource handoff (stop LED task when Python controls LEDs)

---

### Phase 3: App Discovery & Launcher (Week 3-4)

#### 3.1 Filesystem Structure

```
/pyapps/
├── snake/
│   ├── app.py          # Entry point (required)
│   ├── manifest.json   # Metadata
│   └── assets/         # Optional: images, data
├── clock/
│   ├── app.py
│   └── manifest.json
└── textscroll/
    └── app.py
```

#### 3.2 Manifest Format

```json
{
  "name": "Snake Game",
  "version": "1.0",
  "author": "Username",
  "description": "Classic snake game",
  "entry": "app.py",
  "icon": "icon.raw",
  "requires": ["display", "buttons"]
}
```

#### 3.3 App Discovery

- [ ] Implement `scan_pyapps()` in C:
  - Mount `/pyapps` partition
  - Scan for directories with `app.py`
  - Parse `manifest.json`
  - Populate `g_pyapp_list[]` array

- [ ] Add "Python Apps" submenu to main menu:
  ```c
  menu_item_t pyapps_menu = {
      .label = "Python Apps",
      .type = MENU_TYPE_SUBMENU,
      .submenu = &g_pyapps_submenu  // Dynamically populated
  };
  ```

#### 3.4 App Launcher

- [ ] Implement `launch_pyapp(const char *app_path)`:
  1. Send command to MicroPython task on CPU1
  2. Hand off display/button control
  3. Stop LED animations (or delegate to Python)
  4. Wait for app exit

- [ ] MicroPython side: `run_app(path)`:
  ```python
  # In micropython_task.c
  def run_app(path):
      sys.path.insert(0, path)
      try:
          import app
          app.main()  # Entry point
      except Exception as e:
          badge.display.error(str(e))
      finally:
          badge.exit()
  ```

---

### Phase 4: Example App - Snake Game (Week 4)

#### 4.1 Snake Implementation

**File:** `/pyapps/snake/app.py`

```python
import badge
import time
import random

GRID_W = 32
GRID_H = 17
CELL_SIZE = 10

class Snake:
    def __init__(self):
        self.body = [(GRID_W // 2, GRID_H // 2)]
        self.dir = (1, 0)
        self.food = self.spawn_food()
        self.score = 0
        self.running = True

    def spawn_food(self):
        while True:
            pos = (random.randint(0, GRID_W-1), random.randint(0, GRID_H-1))
            if pos not in self.body:
                return pos

    def update(self):
        head = self.body[0]
        new_head = ((head[0] + self.dir[0]) % GRID_W,
                    (head[1] + self.dir[1]) % GRID_H)
        
        # Check collision with self
        if new_head in self.body:
            self.running = False
            return
        
        self.body.insert(0, new_head)
        
        # Check food
        if new_head == self.food:
            self.score += 10
            self.food = self.spawn_food()
        else:
            self.body.pop()

    def draw(self):
        badge.display.clear(0x0000)  # Black
        
        # Draw snake (green)
        for x, y in self.body:
            badge.display.rect(x * CELL_SIZE, y * CELL_SIZE, 
                             CELL_SIZE-1, CELL_SIZE-1, 0x07E0)
        
        # Draw food (red)
        fx, fy = self.food
        badge.display.rect(fx * CELL_SIZE, fy * CELL_SIZE, 
                         CELL_SIZE-1, CELL_SIZE-1, 0xF800)
        
        # Score
        badge.display.text(f"Score: {self.score}", 5, 5, 0xFFFF)
        badge.display.show()

    def handle_input(self):
        btn = badge.buttons.get()
        if btn['up'] and self.dir != (0, 1):
            self.dir = (0, -1)
        elif btn['down'] and self.dir != (0, -1):
            self.dir = (0, 1)
        elif btn['left'] and self.dir != (1, 0):
            self.dir = (-1, 0)
        elif btn['right'] and self.dir != (-1, 0):
            self.dir = (1, 0)
        elif btn['b']:  # Exit
            self.running = False

def main():
    snake = Snake()
    
    while snake.running:
        snake.handle_input()
        snake.update()
        snake.draw()
        time.sleep(0.15)  # Game speed
    
    # Game over
    badge.display.clear(0x0000)
    badge.display.text("Game Over!", 100, 70, 0xF800)
    badge.display.text(f"Score: {snake.score}", 100, 90, 0xFFFF)
    badge.display.text("Press B to exit", 80, 120, 0xFFFF)
    badge.display.show()
    
    while not badge.buttons.get()['b']:
        time.sleep(0.1)
    
    badge.exit()

if __name__ == '__main__':
    main()
```

**File:** `/pyapps/snake/manifest.json`

```json
{
  "name": "Snake Game",
  "version": "1.0",
  "author": "Badge Community",
  "description": "Classic snake game with D-pad controls",
  "entry": "app.py"
}
```

---

### Phase 5: OTA Update System (Week 5)

#### 5.1 OTA Infrastructure

- [ ] Add `components/ota_manager/`:
  - WiFi connection manager
  - HTTP/HTTPS OTA client
  - Rollback on boot failure

#### 5.2 OTA Menu Integration

- [ ] Add "System Update" menu item
- [ ] Show update progress on display
- [ ] Validate firmware signature (optional but recommended)

#### 5.3 OTA API for Python

```python
# Allow Python apps to check for updates
import badge.ota

if badge.ota.check_update():
    badge.ota.download_and_install()
```

---

### Phase 6: Development Tools (Week 5-6)

#### 6.1 App Upload Tool

**Script:** `tools/upload_app.py`

```python
#!/usr/bin/env python3
"""Upload MicroPython app to badge via serial."""

import serial
import sys
import os
from pathlib import Path

def upload_app(port, app_dir):
    """Upload app directory to /pyapps/"""
    ser = serial.Serial(port, 115200)
    
    # Enter raw REPL mode
    ser.write(b'\x01')  # Ctrl-A
    
    for root, dirs, files in os.walk(app_dir):
        for file in files:
            src = os.path.join(root, file)
            dst = f"/pyapps/{app_dir}/{file}"
            
            with open(src, 'rb') as f:
                data = f.read()
            
            # MicroPython file write
            cmd = f"with open('{dst}', 'wb') as f: f.write({data!r})\n"
            ser.write(cmd.encode())
    
    print(f"✓ Uploaded {app_dir}")

if __name__ == '__main__':
    upload_app(sys.argv[1], sys.argv[2])
```

Usage:
```bash
python tools/upload_app.py /dev/ttyUSB0 snake/
```

#### 6.2 REPL Access

- [ ] Add menu item "Python REPL" to enter interactive mode
- [ ] Serial console shows MicroPython prompt on CPU1

---

## Memory Budget

### RAM Allocation (ESP32-S3: ~512KB total)

| Component | CPU | Size | Notes |
|-----------|-----|------|-------|
| FreeRTOS kernel | CPU0 | 32KB | Scheduler, tasks |
| Display framebuffer | CPU0 | 106KB | 320×170×2 bytes (RGB565) |
| FreeRTOS tasks | CPU0 | 50KB | Stacks for input/display/led tasks |
| MicroPython heap | CPU1 | 256KB | User scripts, objects |
| Other (drivers, libs) | Both | 68KB | SPI buffers, etc. |

**Total:** ~512KB (fits with careful management)

**Optimization:** Use PSRAM if available (enables larger framebuffer cache, bigger Python heap).

---

## Testing Strategy

### Unit Tests

1. **Partition mounting**: Verify FAT filesystem accessible
2. **App discovery**: Scan `/pyapps/`, parse manifests
3. **Inter-core messaging**: CPU0↔CPU1 queue latency
4. **API bridge**: Call each `badge.*` function from Python

### Integration Tests

1. **Menu navigation**: Launch app from menu, exit back to menu
2. **Resource handoff**: Verify display ownership transfers correctly
3. **Button routing**: Buttons work in both FreeRTOS and Python modes
4. **LED control**: Python can override FreeRTOS LED patterns
5. **Crash handling**: Python exception doesn't crash FreeRTOS

### User Acceptance Tests

1. Upload snake game via serial
2. Navigate menu to "Python Apps" → "Snake Game"
3. Play game with D-pad, exit with B button
4. Verify FreeRTOS features still work (LED menu, settings)

---

## Build & Flash Instructions

### 1. Enable MicroPython Support

```bash
cd components/
git submodule add https://github.com/micropython/micropython.git
cd micropython && git checkout v1.23.0  # Or latest stable
make -C mpy-cross  # Build cross-compiler
```

### 2. Update Configuration

```bash
# Copy new partition table
cp partitions_ota_micropython.csv .

# Update sdkconfig
idf.py menuconfig
# Navigate to: Partition Table → Custom partition table CSV
# Set filename to: partitions_ota_micropython.csv
```

### 3. Build & Flash

```bash
# Clean build
idf.py fullclean

# Build firmware
idf.py build

# Flash everything (erase first for partition table change)
idf.py -p /dev/ttyUSB0 erase_flash
idf.py -p /dev/ttyUSB0 flash monitor
```

### 4. Upload Example App

```bash
python tools/upload_app.py /dev/ttyUSB0 pyapps/snake/
```

---

## Migration Path (Existing Users)

### For Users with Current Firmware

1. **Backup settings** (NVS will be erased due to partition change)
2. **Flash new firmware** with updated partition table
3. **Restore settings** via menu or serial commands
4. **Verify all FreeRTOS features work** (LED modes, sensors, etc.)
5. **Optional:** Upload Python apps

### Backwards Compatibility

- All existing FreeRTOS features remain functional
- Badge works normally without any Python apps installed
- Python Apps menu only shows if `/pyapps/` has valid apps

---

## Security Considerations

### Sandboxing

- MicroPython runs with limited memory (256KB heap)
- No direct hardware register access (only via `badge` API)
- File access restricted to `/pyapps/` partition

### Code Signing (Future Enhancement)

- Sign `.py` files with developer key
- Verify signatures before execution
- Reject unsigned apps in production firmware

---

## Performance Expectations

| Metric | Value |
|--------|-------|
| App launch time | <500ms |
| Display update latency | <50ms (buffered) |
| Button response | <10ms (interrupt-driven) |
| Python script speed | ~500KB/s bytecode execution |
| LED update rate | 60 FPS (both FreeRTOS and Python) |

---

## Future Enhancements

### Phase 7+ (Post-MVP)

- [ ] WiFi networking API for Python apps
- [ ] Web-based app uploader (HTTP server on badge)
- [ ] App marketplace / repository integration
- [ ] More example apps (calculator, sensor logger, games)
- [ ] Python debugger integration (via WebREPL)
- [ ] Bluetooth API for inter-badge communication
- [ ] Save/load app state (persistence API)

---

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Flash space too small | High | Compress OTA partitions; reduce factory size |
| RAM exhaustion | High | Monitor heap usage; limit Python heap to 256KB |
| CPU1 interference with CPU0 | Medium | Strict task priorities; test inter-core latency |
| Python crashes freeze badge | Medium | Watchdog timer on CPU1; exception handler returns to menu |
| App uploads corrupt filesystem | Low | Validate uploads; add filesystem check on boot |

---

## Timeline Summary

| Phase | Duration | Deliverables |
|-------|----------|--------------|
| 1. Foundation | 2 weeks | Partition table, filesystem, basic MP task |
| 2. API Bridge | 1 week | `badge` module with display/LED/button APIs |
| 3. App Launcher | 1 week | Menu integration, app discovery |
| 4. Example App | 3 days | Snake game fully functional |
| 5. OTA System | 1 week | Firmware update infrastructure |
| 6. Dev Tools | 3 days | Upload tool, REPL access |
| **Total** | **5-6 weeks** | Production-ready hybrid firmware |

---

## Success Criteria

- ✅ FreeRTOS features work identically to current firmware
- ✅ Users can upload Python apps via serial
- ✅ Apps appear in menu and launch correctly
- ✅ Snake game playable at 60 FPS
- ✅ Exit from app returns cleanly to menu
- ✅ OTA updates work without data loss
- ✅ No CPU0/CPU1 resource conflicts

---

## Next Steps

1. **Review this plan** - confirm approach and priorities
2. **Create feature branch**: `git checkout -b feature/micropython-integration`
3. **Start Phase 1**: Create partition table and test FAT mounting
4. **Incremental commits**: Test each component before proceeding

---

## References

- [MicroPython ESP32 Port](https://github.com/micropython/micropython/tree/master/ports/esp32)
- [ESP-IDF OTA Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html)
- [ESP32 Partition Tables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/partition-tables.html)
- [FreeRTOS Symmetric Multiprocessing](https://www.freertos.org/symmetric-multiprocessing-introduction.html)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-21  
**Author:** GitHub Copilot (with user requirements)
