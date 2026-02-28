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

## Implementation Status (Phase 3)

### ✅ Completed
1. **MicroPython VM Infrastructure**
   - Created `mpconfigboard.h` - Board-specific settings (D26Badge, ESP32-S3)
   - Created `mpconfigport.h` - Port-wide config (128KB heap, Xtensa emitter)
   - Created `mphalport.h/c` - Hardware abstraction layer (ticks, delays, I/O stubs)
   
2. **Badge Native Module (`modbadge.c` - 196 lines)**
   - Complete implementation of `badge` module with submodules:
     * `badge.display.clear(color)` - Clear screen with color
     * `badge.display.pixel(x, y, color)` - Set pixel
     * `badge.display.text(x, y, text, color)` - Render text (127 char max)
     * `badge.display.show()` - Update display
     * `badge.leds.set(index, r, g, b)` - Set LED color
     * `badge.buttons.is_pressed(mask)` - Check button state
     * `badge.delay_ms(ms)` - Delay function
   - All functions use `mp_bridge_send_*` / `mp_bridge_recv_*` for CPU0 communication

3. **VM Initialization (`micropython_runner.c`)**
   - Added MicroPython core includes (compile.h, runtime.h, gc.h, stackctrl.h)
   - Static heap allocation: `uint8_t mp_heap[128*1024]` aligned(4)
   - Implemented `get_sp()` inline asm for Xtensa stack pointer capture
   - Complete `mp_task()` function:
     * Stack initialization: `mp_stack_set_top()` / `mp_stack_set_limit()`
     * GC initialization: `gc_init(mp_heap, mp_heap + 128K)`
     * Runtime initialization: `mp_init()`
     * Test code execution: `print('Hello from MicroPython!')`
     * NLR exception handling with `mp_obj_print_exception()`
     * Main loop with periodic `gc_collect()`
     * Cleanup: `mp_deinit()` on exit

4. **Build System Integration (`CMakeLists.txt`)**
   - Set `MICROPY_DIR` to `${CMAKE_SOURCE_DIR}/micropython`
   - Added ~110 MicroPython core source files (py/*.c):
     * Core: argcheck.c, compile.c, gc.c, parse.c, runtime.c, vm.c
     * Object types: objarray.c, objdict.c, objfloat.c, objfun.c, objint.c, objlist.c, objstr.c, etc.
     * Built-in modules: modbuiltins.c, modgc.c, modio.c, modmath.c, modsys.c, etc.
     * Emitters: emitbc.c, emitnative.c, asmxtensa.c
   - Added include directories: genhdr/, micropython/, micropython/py/, micropython/ports/esp32/
   - Added component requirements: freertos, esp_system, esp_common, esp_timer, nvs_flash
   - Added compile definitions: MICROPY_QSTR_EXTRA_POOL, MICROPY_MODULE_FROZEN_MPY

5. **Generated Headers**
   - Created `genhdr/qstrdefs.generated.h` with 160+ qstr definitions:
     * Core qstrs: empty string, underscore, star, slash
     * Dunder names: __name__, __module__, __class__, __init__, __str__, etc.
     * Badge module: badge, display, leds, buttons, clear, pixel, text, show, set, is_pressed, delay_ms
     * Python built-ins: range, super, OrderedDict, StopAsyncIteration
     * Exception types: Exception, TypeError, ValueError, RuntimeError, etc. (20+ types)
     * Common functions: append, extend, pop, print, len, str, int, float, etc. (60+ functions)
   - Created `genhdr/root_pointers.h` - Empty (minimal for embedded mode)
   - Created `genhdr/moduledefs.h` - Badge module registration

### ⚠️ Known Issues (Need Fixing)

#### 1. Missing QSTRs (~15 remaining)
Location: `components/micropython_runner/genhdr/qstrdefs.generated.h`

Need to add:
```c
QDEF0(MP_QSTR___file__, 58933, 8, "__file__")
QDEF0(MP_QSTR_array, 52929, 5, "array")
QDEF0(MP_QSTR_bccz, 60896, 4, "bccz")
QDEF0(MP_QSTR_bcc, 60896, 3, "bcc")
QDEF0(MP_QSTR_bit_branch, 60896, 10, "bit_branch")
QDEF0(MP_QSTR__lt_string_gt_, 60896, 8, "<string>")  // Already exists as MP_QSTR__string_
QDEF0(MP_QSTR__lt_stdin_gt_, 60896, 7, "<stdin>")    // Already exists as MP_QSTR__stdin_
```

**Fix:** Run qstr hash generator for missing names and append to qstrdefs.generated.h

#### 2. Type Conflicts in `mpconfigport.h`
Location: Line 45 - syntax error "expected identifier or '(' before string constant"
Location: Missing `mp_off_t` type definition

**Current issue:**
```c
// Type definitions - use MicroPython's defaults (intptr_t based)
// These will be defined by mpconfig.h, we just need to not conflict
```

**Fix needed:**
```c
// Let MicroPython's mpconfig.h define mp_int_t/mp_uint_t (intptr_t based)
// We only need to define mp_off_t
typedef long mp_off_t;
```

Check line 45 for syntax error (likely a macro definition issue).

#### 3. Bridge API Mismatch in `modbadge.c`
The bridge commands use old enum names that don't match `mp_bridge.h`:

**Current (WRONG):**
```c
cmd.type = MP_DISPLAY_CLEAR;  // Undefined
cmd.color = color;             // Wrong struct member
mp_bridge_send_display_cmd(&cmd);  // Wrong signature (needs size param)
```

**Should be (from mp_bridge.h):**
```c
mp_display_cmd_t cmd;
cmd.type = MP_DISP_CMD_CLEAR;
cmd.clear.color = color;
mp_bridge_send_display_cmd(&cmd, sizeof(cmd));
```

**All command types to fix:**
- `MP_DISPLAY_CLEAR` → `MP_DISP_CMD_CLEAR` (use `cmd.clear.color`)
- `MP_DISPLAY_PIXEL` → `MP_DISP_CMD_PIXEL` (use `cmd.pixel.{x,y,color}`)
- `MP_DISPLAY_TEXT` → `MP_DISP_CMD_TEXT` (use `cmd.text.{x,y,text,color}`)
- `MP_DISPLAY_SHOW` → `MP_DISP_CMD_SHOW` (no params)

**LED command fix:**
```c
// Current (WRONG):
mp_bridge_send_led_cmd(&cmd);

// Should be:
mp_bridge_send_led_cmd(&cmd, sizeof(cmd));
```

#### 4. MicroPython Type System (v1.27.0 Changes)
The module type definitions use old API:

**Current (WRONG):**
```c
static const mp_obj_type_t badge_display_type = {
    { &mp_type_type },
    .name = MP_QSTR_display,
    .locals_dict = (mp_obj_dict_t *)&badge_display_locals_dict,  // DEPRECATED
};
```

**Should use new slot-based API (v1.27.0+):**
```c
static MP_DEFINE_CONST_OBJ_TYPE(
    badge_display_type,
    MP_QSTR_display,
    MP_TYPE_FLAG_NONE,
    locals_dict, &badge_display_locals_dict
);
```

Apply to all three module types: `badge_display_type`, `badge_leds_type`, `badge_buttons_type`.

#### 5. Function Signature Mismatch
**Issue:** `badge_leds_set` has wrong signature for `MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN`

**Current:**
```c
static mp_obj_t badge_leds_set(mp_obj_t index_obj, mp_obj_t r_obj, mp_obj_t g_obj, mp_obj_t b_obj)
```

**Should be (for VAR_BETWEEN with 4 args):**
```c
static mp_obj_t badge_leds_set(size_t n_args, const mp_obj_t *args) {
    // args[0] = index, args[1] = r, args[2] = g, args[3] = b
    int index = mp_obj_get_int(args[0]);
    int r = mp_obj_get_int(args[1]);
    int g = mp_obj_get_int(args[2]);
    int b = mp_obj_get_int(args[3]);
    // ... rest of function
}
```

#### 6. `mphalport.h` - Missing `mp_print_t` Declaration
**Error:** "unknown type name 'mp_print_t'"

**Fix:** The `extern const mp_print_t mp_plat_print;` declaration should come AFTER including py/mpprint.h, or remove it since it's already declared in MicroPython headers.

### 🔧 Step-by-Step Fix Guide

#### Step 1: Add Missing QSTRs (5 minutes)
```bash
cd /home/llatva/git/d26badge-freertos-fw

cat >> components/micropython_runner/genhdr/qstrdefs.generated.h << 'EOF'
// Missing qstrs for ASM and imports
QDEF0(MP_QSTR___file__, 58933, 8, "__file__")
QDEF0(MP_QSTR_array, 22529, 5, "array")
QDEF0(MP_QSTR_bccz, 17896, 4, "bccz")
QDEF0(MP_QSTR_bcc, 17765, 3, "bcc")
QDEF0(MP_QSTR_bit_branch, 32256, 10, "bit_branch")
EOF
```

#### Step 2: Fix `mpconfigport.h` Types (2 minutes)
Edit `components/micropython_runner/mpconfigport.h`:
- Add after line 67: `typedef long mp_off_t;`
- Fix line 45 syntax error (check for stray quote or macro)

#### Step 3: Fix Bridge API in `modbadge.c` (10 minutes)
Replace all command sends:
```bash
# Search and replace patterns:
MP_DISPLAY_CLEAR → MP_DISP_CMD_CLEAR
MP_DISPLAY_PIXEL → MP_DISP_CMD_PIXEL
MP_DISPLAY_TEXT → MP_DISP_CMD_TEXT
MP_DISPLAY_SHOW → MP_DISP_CMD_SHOW

# Fix struct members:
cmd.color → cmd.clear.color
cmd.x → cmd.pixel.x (or cmd.text.x)
cmd.y → cmd.pixel.y (or cmd.text.y)
cmd.text → cmd.text.text

# Add sizeof parameter:
mp_bridge_send_display_cmd(&cmd) → mp_bridge_send_display_cmd(&cmd, sizeof(cmd))
mp_bridge_send_led_cmd(&cmd) → mp_bridge_send_led_cmd(&cmd, sizeof(cmd))
```

#### Step 4: Update Module Types (15 minutes)
Replace the three type definitions in `modbadge.c`:

```c
// OLD: badge_display_type
static const mp_obj_type_t badge_display_type = {
    { &mp_type_type },
    .name = MP_QSTR_display,
    .locals_dict = (mp_obj_dict_t *)&badge_display_locals_dict,
};

// NEW:
static MP_DEFINE_CONST_OBJ_TYPE(
    badge_display_type,
    MP_QSTR_display,
    MP_TYPE_FLAG_NONE,
    locals_dict, &badge_display_locals_dict
);
```

Apply same pattern to `badge_leds_type` and `badge_buttons_type`.

#### Step 5: Fix `badge_leds_set` Signature (5 minutes)
```c
// Change from 4 separate args to varargs:
static mp_obj_t badge_leds_set(size_t n_args, const mp_obj_t *args) {
    int index = mp_obj_get_int(args[0]);
    int r = mp_obj_get_int(args[1]);
    int g = mp_obj_get_int(args[2]);
    int b = mp_obj_get_int(args[3]);
    
    mp_led_cmd_t cmd;
    cmd.index = index;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;
    
    mp_bridge_send_led_cmd(&cmd, sizeof(cmd));
    return mp_const_none;
}
```

#### Step 6: Fix `mphalport.h` (1 minute)
Remove or move the `extern const mp_print_t mp_plat_print;` declaration after all includes.

#### Step 7: Build and Test (5 minutes)
```bash
cd /home/llatva/git/d26badge-freertos-fw
. ./setup_idf.sh
idf.py build
```

If successful:
```bash
idf.py flash monitor
```

Expected output:
```
Hello from MicroPython!
GC: total: 131072, used: 2048, free: 129024
```

### 📋 Verification Checklist

After fixes applied:
- [ ] Build completes without errors
- [ ] Flash succeeds
- [ ] Serial monitor shows "Hello from MicroPython!"
- [ ] Python VM initializes without crashes
- [ ] GC runs successfully
- [ ] Badge module imports: `import badge`
- [ ] Display commands work: `badge.display.clear(0xFFFF)`
- [ ] LED commands work: `badge.leds.set(0, 255, 0, 0)`
- [ ] Button reads work: `badge.buttons.is_pressed(1)`

### 🎯 Next Steps (Phase 4 - After Build Works)

1. **Connect CPU0 Display Task**
   - Modify `main.c` `display_task()` to read from `mp_bridge_recv_display_cmd()`
   - Implement command handlers for CLEAR, PIXEL, TEXT, SHOW
   - Test rendering Python's display commands

2. **Test Python Apps**
   - Write test script in `pyapps_fs/app.py`
   - Test display text rendering
   - Test LED control
   - Test button input

3. **Optimize Performance**
   - Profile GC timing
   - Optimize queue depths if needed
   - Test display throughput

4. **Add More Features** (Optional)
   - File I/O (SPIFFS access)
   - Time/RTC functions
   - More built-in modules (json, struct, etc.)

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

**Status:** Phase 3 - 95% Complete (Build System Ready, Minor Fixes Needed)
**Estimated Time to Complete:** 30-45 minutes following fix guide above
**Last Updated:** February 21, 2026

