# MicroPython Integration Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32-S3 Badge                            │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                        CPU0 (FreeRTOS)                    │  │
│  │  ┌──────────┐  ┌───────────┐  ┌─────────┐  ┌─────────┐  │  │
│  │  │ Display  │  │  Button   │  │   LED   │  │  Menu   │  │  │
│  │  │   Task   │  │    ISR    │  │  Task   │  │   UI    │  │  │
│  │  └────┬─────┘  └─────┬─────┘  └────┬────┘  └─────────┘  │  │
│  │       │              │              │                      │  │
│  │       │              │              │                      │  │
│  │  ┌────▼──────────────▼──────────────▼────────────────┐   │  │
│  │  │            MP Bridge (FreeRTOS Queues)            │   │  │
│  │  │  - display_cmd_queue (10)                         │   │  │
│  │  │  - button_event_queue (20)                        │   │  │
│  │  │  - led_cmd_queue (10)                             │   │  │
│  │  │  - display_lock_sem                               │   │  │
│  │  └───────────────────┬───────────────────────────────┘   │  │
│  └────────────────────────────────────────────────────────────┘  │
│                          │                                        │
│                Cross-Core Communication                          │
│                          │                                        │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                        CPU1 (Python VM)                    │  │
│  │  ┌──────────────────────────────────────────────────────┐ │  │
│  │  │           MicroPython Interpreter                     │ │  │
│  │  │  ┌──────────────────────────────────────────────┐   │ │  │
│  │  │  │  Python App (app.py)                          │   │ │  │
│  │  │  │  ┌──────────────────────────────────┐        │   │ │  │
│  │  │  │  │  import badge                     │        │   │ │  │
│  │  │  │  │  badge.display.text(10, 10, "Hi") │        │   │ │  │
│  │  │  │  │  badge.leds.set(0, 255, 0, 0)     │        │   │ │  │
│  │  │  │  │  if badge.buttons.is_pressed(1):  │        │   │ │  │
│  │  │  │  │      badge.exit()                 │        │   │ │  │
│  │  │  │  └──────────────────────────────────┘        │   │ │  │
│  │  │  └──────────────────────────────────────────────┘   │ │  │
│  │  │                       │                              │ │  │
│  │  │  ┌──────────────────────────────────────────────┐   │ │  │
│  │  │  │  Badge Native Module (C)                     │   │ │  │
│  │  │  │  - badge.display.*  → send to queue         │   │ │  │
│  │  │  │  - badge.leds.*     → send to queue         │   │ │  │
│  │  │  │  - badge.buttons.*  → read from queue       │   │ │  │
│  │  │  │  - badge.exit()     → signal exit           │   │ │  │
│  │  │  └──────────────────────────────────────────────┘   │ │  │
│  │  └──────────────────────────────────────────────────────┘ │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Flash Storage                           │  │
│  │  /pyapps partition (1MB FAT)                               │  │
│  │  ├── test/                                                  │  │
│  │  │   └── app.py                                            │  │
│  │  ├── game1/                                                 │  │
│  │  │   ├── app.py                                            │  │
│  │  │   └── assets/                                           │  │
│  │  └── demo/                                                  │  │
│  │      └── app.py                                            │  │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Component Structure

```
components/micropython_runner/
├── include/
│   ├── micropython_runner.h    # Public API for CPU0 to call
│   │   - micropython_runner_init()
│   │   - micropython_runner_deinit()
│   │   - micropython_load_app(path)
│   │   - micropython_stop_app()
│   │   - micropython_is_running()
│   │
│   └── mp_bridge.h              # Bridge structures and APIs
│       - mp_display_cmd_t       # Display command structure
│       - mp_button_event_t      # Button event structure
│       - mp_led_cmd_t           # LED command structure
│       - mp_bridge_init()
│       - mp_bridge_send_display_cmd()
│       - mp_bridge_recv_display_cmd()
│       - mp_bridge_send_button_event()
│       - mp_bridge_recv_button_event()
│       - mp_bridge_send_led_cmd()
│       - mp_bridge_recv_led_cmd()
│       - mp_bridge_display_lock()
│       - mp_bridge_display_unlock()
│
├── micropython_runner.c         # MP task management (CPU1)
│   - Allocates MicroPython heap (128KB)
│   - Initializes MicroPython VM (mp_init, gc_init)
│   - Loads and executes Python apps
│   - Handles exceptions and cleanup
│
├── mp_bridge.c                  # Bridge implementation
│   - Creates FreeRTOS queues
│   - Implements send/recv functions
│   - Thread-safe queue operations
│
├── modbadge.c                   # Badge native module (future)
│   - Implements Python badge module
│   - Wraps bridge calls as Python functions
│
├── mpconfigport.h               # MicroPython configuration
│   - Minimal feature set (no network/bluetooth)
│   - 128KB heap, Xtensa emitter
│   - Badge-specific settings
│
└── CMakeLists.txt               # Component build
    - Registers component sources
    - Links MicroPython library
    - Sets include paths
```

## Data Flow Examples

### Example 1: Python Draws Text
```
┌─────────────┐
│ Python Code │  badge.display.text(10, 10, "Hello", 0xFFFF)
└──────┬──────┘
       │
       ▼
┌──────────────────┐
│ modbadge.c       │  mp_bridge_send_display_cmd(&cmd)
│ display_text()   │
└──────┬───────────┘
       │
       ▼
┌──────────────────────┐
│ FreeRTOS Queue       │  xQueueSend(display_cmd_queue, ...)
│ display_cmd_queue    │
└──────┬───────────────┘
       │
       ▼
┌──────────────────┐
│ CPU0             │  mp_bridge_recv_display_cmd(&cmd, 0)
│ display_task()   │  // In main loop
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│ st7789_draw_*()  │  Actual hardware rendering
└──────────────────┘
```

### Example 2: Button Press Event
```
┌─────────────┐
│ Hardware     │  Button A pressed
│ GPIO ISR     │
└──────┬───────┘
       │
       ▼
┌──────────────────┐
│ CPU0             │  if (micropython_is_running())
│ button_isr()     │      mp_bridge_send_button_event(0x01, true)
└──────┬───────────┘
       │
       ▼
┌──────────────────────┐
│ FreeRTOS Queue       │  xQueueSend(button_event_queue, ...)
│ button_event_queue   │
└──────┬───────────────┘
       │
       ▼
┌──────────────────┐
│ CPU1             │  mp_bridge_recv_button_event(&evt, 0)
│ modbadge.c       │  // In Python event loop
└──────┬───────────┘
       │
       ▼
┌─────────────┐
│ Python Code │  if badge.buttons.is_pressed(0x01):
│              │      # Handle button press
└──────────────┘
```

## Memory Layout

```
ESP32-S3 Memory Map:

┌─────────────────────────────────────┐  0x00000000
│  ROM (384KB)                        │
├─────────────────────────────────────┤  0x40000000
│  Internal SRAM (512KB)              │
│  ┌─────────────────────────────┐   │
│  │ FreeRTOS Heap (~350KB)      │   │
│  │  - CPU0 tasks               │   │
│  │  - Display buffers          │   │
│  │  - Network stacks           │   │
│  │  ┌─────────────────────┐   │   │
│  │  │ MicroPython Heap    │   │   │
│  │  │ (128KB)             │   │   │
│  │  │ - GC managed        │   │   │
│  │  │ - Python objects    │   │   │
│  │  │ - String intern     │   │   │
│  │  └─────────────────────┘   │   │
│  └─────────────────────────────┘   │
├─────────────────────────────────────┤
│  PSRAM (8MB) - Currently unused     │
│  (Future: Can move Python heap here)│
├─────────────────────────────────────┤  0x42000000
│  Flash (16MB Octal)                 │
│  ┌─────────────────────────────┐   │
│  │ Bootloader (24KB)           │   │  0x00000000
│  │ Partition Table (8KB)       │   │  0x00008000
│  │ NVS (24KB)                  │   │  0x00009000
│  │ PHY Init (4KB)              │   │  0x0000f000
│  ├─────────────────────────────┤   │
│  │ factory partition (4MB)     │   │  0x00020000
│  │  - Badge FreeRTOS App       │   │
│  │  - ~380KB used              │   │
│  │  - 3.6MB free               │   │
│  ├─────────────────────────────┤   │
│  │ ota_0 partition (4MB)       │   │  0x00420000
│  │ ota_1 partition (4MB)       │   │  0x00820000
│  ├─────────────────────────────┤   │
│  │ pyapps partition (1MB FAT)  │   │  0x00c20000
│  │  - Python apps              │   │
│  │  - Wear levelling           │   │
│  └─────────────────────────────┘   │
└─────────────────────────────────────┘
```

## Task Priorities and CPU Assignment

```
Priority 24+: WiFi/BT (ESP-IDF internal)
Priority 20:  Audio Task (CPU0)
Priority 15:  Display Task (CPU0)
Priority 10:  LED Task (CPU0)
Priority 5:   MicroPython Task (CPU1) ← Python VM runs here
Priority 5:   Button Task (CPU0)
Priority 5:   Menu UI Task (CPU0)
Priority 1:   Idle Task (both cores)
```

**Key Design Rule**: MicroPython task pinned to CPU1 prevents blocking CPU0 hardware tasks.

## API Reference Summary

### C API (for integration)

```c
// Initialize MicroPython on CPU1
esp_err_t micropython_runner_init(void);

// Clean shutdown
esp_err_t micropython_runner_deinit(void);

// Load and execute Python app
esp_err_t micropython_load_app(const char *app_path);

// Stop currently running app
esp_err_t micropython_stop_app(void);

// Check if Python is running
bool micropython_is_running(void);
```

### Python API (for app developers)

```python
import badge

# Display control
badge.display.clear(color)                    # Clear screen to color (RGB565)
badge.display.pixel(x, y, color)              # Draw single pixel
badge.display.text(x, y, text, color)         # Draw text
badge.display.rect(x, y, w, h, color, fill)   # Draw rectangle
badge.display.show()                          # Flush framebuffer to screen

# LED control
badge.leds.set(index, r, g, b)                # Set LED color (0-4)
badge.leds.clear()                            # Turn off all LEDs

# Button input
badge.buttons.is_pressed(button_mask)         # Check button state
# Button masks: 0x01=A, 0x02=B, 0x04=UP, 0x08=DOWN, 0x10=LEFT, 0x20=RIGHT

# Badge settings
badge.settings.get(key)                       # Get persistent setting
badge.settings.set(key, value)                # Save persistent setting

# Utility
badge.exit()                                  # Exit app, return to menu
badge.delay_ms(ms)                            # Sleep (non-blocking)
```

## Build System Integration

### Current Build Flow
```
1. ESP-IDF builds main badge firmware (CPU0 code)
2. micropython_runner component included
3. Component links against MicroPython libraries (future)
4. Final ELF combines everything
5. Binary flashed to factory partition
```

### Future MicroPython Build Steps (Phase 3)
```
1. Build mpy-cross (MicroPython cross-compiler)
   cd micropython/mpy-cross && make

2. Build MicroPython core library
   cd micropython/py && make libmicropython.a

3. Link in ESP-IDF component
   target_link_libraries(micropython_runner libmicropython.a)

4. Compile badge native module
   gcc modbadge.c -o modbadge.o

5. Link everything into badge firmware
```

## Configuration Options

### mpconfigport.h Key Settings
```c
#define MICROPY_GC_INITIAL_HEAP_SIZE   (128 * 1024)  // Python heap
#define MICROPY_PY_THREAD              (0)           // No threading
#define MICROPY_PY_NETWORK             (0)           // No network
#define MICROPY_PY_BLUETOOTH           (0)           // No Bluetooth
#define MICROPY_EMIT_XTENSAWIN         (1)           // Xtensa emitter
#define MICROPY_VFS                    (1)           // VFS enabled
```

### Partition Table
```csv
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x20000, 0x400000,
ota_0,    app,  ota_0,   0x420000, 0x400000,
ota_1,    app,  ota_1,   0x820000, 0x400000,
pyapps,   data, fat,     0xc20000, 0x100000,
```

## Thread Safety

### Safe Operations
- Bridge queue operations (thread-safe by design)
- Display lock/unlock around framebuffer access
- Button events sent from ISR context (safe with xQueueSendFromISR)

### Unsafe Operations (Avoid!)
- Direct framebuffer access from Python (use bridge only)
- GPIO manipulation from Python (use badge.leds/buttons)
- Calling CPU0 functions directly from CPU1

### Synchronization Primitives
- `display_cmd_queue`: Commands from Python → display task
- `button_event_queue`: Events from ISR → Python
- `led_cmd_queue`: Commands from Python → LED task
- `display_lock_sem`: Mutex for display resource

## Error Handling

### Python Exceptions
```python
try:
    badge.display.text(10, 10, "Test")
    badge.display.show()
except MemoryError:
    badge.display.clear(0xF800)  # Red screen = error
    badge.delay_ms(1000)
    badge.exit()
```

### C Error Codes
```c
ESP_OK                  // Success
ESP_ERR_INVALID_STATE   // MicroPython not initialized
ESP_ERR_INVALID_ARG     // Bad parameter
ESP_ERR_NO_MEM          // Heap allocation failed
ESP_ERR_NOT_SUPPORTED   // Feature not implemented
```

### Recovery Strategy
1. Python exception caught → Display error → Return to menu
2. C-level error → Log error → Clean shutdown → Reset
3. Watchdog timeout → Force restart
4. OOM → Try GC → If fails, clean exit

## Performance Characteristics

### Typical Latencies
- Python → Display command: 1-5 ms (queue + scheduling)
- Button press → Python event: 1-10 ms
- Python loop iteration: 10-100 µs
- Framebuffer flush: 15-30 ms (SPI transfer time)

### Throughput
- Display commands: ~100-200/sec sustainable
- Button events: ~50/sec (debounced)
- LED updates: ~1000/sec peak

### Limitations
- Python GC pause: 5-20 ms (depends on heap usage)
- Single-threaded Python (no async/threading)
- No floating point hardware acceleration (soft-float)

## Future Enhancements

### Potential Improvements
1. Enable PSRAM for larger Python heap
2. Add `async/await` support (requires threading)
3. Native code compilation for performance
4. SD card support for more apps
5. Network/Bluetooth modules (if needed)
6. Frozen modules for faster boot
7. MicroPython REPL over USB
8. OTA updates for Python stdlib

---

*Architecture finalized: Phase 2 Complete*
*Next: Implement MicroPython VM initialization (Phase 3)*
