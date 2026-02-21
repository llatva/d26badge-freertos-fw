# MicroPython Integration Status

## ✅ Phase 2 Completed (Infrastructure)

### What We Built
1. **MicroPython Submodule**
   - Added MicroPython v1.27.0 as git submodule
   - Repository: https://github.com/micropython/micropython.git
   - Commit: 78ff170de ("all: Bump version to 1.27.0")

2. **micropython_runner Component**
   - Created complete component structure at `components/micropython_runner/`
   - Files:
     - `include/micropython_runner.h` - Public API (init/deinit/load_app/stop_app/is_running)
     - `include/mp_bridge.h` - CPU0↔CPU1 bridge definitions
     - `micropython_runner.c` - Task management (CPU1)
     - `mp_bridge.c` - Bridge implementation (FreeRTOS queues/mutexes)
     - `CMakeLists.txt` - Component registration
     - `mpconfigport.h` - Minimal MicroPython config (NEW)

3. **Bridge Infrastructure (CPU0 ↔ CPU1 Communication)**
   - 3 FreeRTOS queues:
     - `display_cmd_queue` (depth 10) - Python → display commands
     - `button_event_queue` (depth 20) - Button press → Python events
     - `led_cmd_queue` (depth 10) - Python → LED commands
   - Display mutex for safe shared access
   - Complete send/recv APIs for all queues

4. **Main Integration**
   - Added `#include "micropython_runner.h"` to `main/main.c`
   - Called `micropython_runner_init()` in `app_main()` after filesystem initialization
   - Added `micropython_runner` to `main/CMakeLists.txt` dependencies

5. **Build & Flash Success**
   - Firmware size: 379 KB (0x5cb70 bytes)
   - Free space: 91% (3.6 MB available in 4 MB partition)
   - Device flashed successfully - application confirmed running

6. **Git Commit**
   - Commit SHA: 827fd2b
   - Message: "Phase 2: Add MicroPython runner infrastructure"
   - All changes staged and committed

### Current Task State
The MicroPython task is running on CPU1 but currently just idles in a loop. The bridge infrastructure is in place but not yet fully utilized. We have the skeleton ready for full VM integration.

---

## 🚧 Phase 3: Next Steps (Actual VM Integration)

### Immediate Priority: Integrate MicroPython VM

#### Step 1: Create Minimal mpconfigboard.h
**File**: `components/micropython_runner/mpconfigboard.h`
- Define board name and version
- Set minimal feature flags
- Configure task/stack sizes

#### Step 2: Update micropython_runner.c with Full VM Init
**Additions needed**:
```c
#include "py/cstack.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/mphal.h"

// In mp_task():
// 1. Allocate heap with heap_caps_malloc()
// 2. Initialize GC with gc_init()
// 3. Call mp_init()
// 4. Add /pyapps to sys.path
// 5. Enter execution loop or REPL
```

#### Step 3: Create Badge Native Module Skeleton
**File**: `components/micropython_runner/modbadge.c`
- Basic module structure using `MP_DEFINE_MODULE`
- Stub functions for:
  - `badge.display.clear()`
  - `badge.display.text(x, y, text, color)`
  - `badge.display.show()`
  - `badge.leds.set(index, r, g, b)`
  - `badge.buttons.is_pressed(button_mask)`
  - `badge.exit()`

#### Step 4: Update Component CMakeLists.txt
**Add MicroPython library linking**:
```cmake
# Link against MicroPython core
target_link_libraries(${COMPONENT_LIB} PRIVATE
    ${CMAKE_SOURCE_DIR}/micropython/py/libmicropython.a
)

# Add MicroPython include directories
target_include_directories(${COMPONENT_LIB} PRIVATE
    ${CMAKE_SOURCE_DIR}/micropython
    ${CMAKE_SOURCE_DIR}/micropython/py
)
```

#### Step 5: Build MicroPython Static Library
We need to compile the MicroPython core separately:
```bash
cd micropython/mpy-cross
make  # Build cross-compiler first

cd ../py
make -f ../../components/micropython_runner/micropython.mk
```

Create `components/micropython_runner/micropython.mk` to build libmicropython.a

#### Step 6: Connect Bridge to CPU0 Tasks
**In main/main.c display_task()**:
```c
mp_display_cmd_t cmd;
while (mp_bridge_recv_display_cmd(&cmd, 0) == ESP_OK) {
    switch (cmd.type) {
        case MP_DISPLAY_CLEAR:
            st7789_fill(&display, cmd.color);
            break;
        case MP_DISPLAY_TEXT:
            // ... render text
            break;
        // ... handle other commands
    }
}
```

**In button ISR handlers**:
```c
if (micropython_is_running()) {
    mp_bridge_send_button_event(button_mask, pressed);
}
```

#### Step 7: Implement App Loading
**In micropython_load_app()**:
```c
// 1. Open `/pyapps/{app_name}/app.py`
// 2. Read file contents
// 3. Call mp_execute_from_str() on CPU1
// 4. Handle Python exceptions
// 5. Return control to menu on exit/crash
```

#### Step 8: Test with Sample App
Create `/pyapps/test/app.py`:
```python
import badge

badge.display.clear(0x0000)  # Black
badge.display.text(10, 10, "Hello from Python!", 0xFFFF)  # White
badge.display.show()

badge.leds.set(0, 255, 0, 0)  # Red LED 0

while True:
    if badge.buttons.is_pressed(0x01):  # Button A
        badge.exit()
    badge.delay_ms(10)
```

---

## 📋 Full TODO List (Prioritized)

### High Priority (Next)
- [x] ~~Add MicroPython submodule v1.27.0~~
- [x] ~~Create micropython_runner component structure~~
- [x] ~~Implement bridge queues (display/button/LED)~~
- [x] ~~Create MP task on CPU1~~
- [x] ~~Integrate into main.c~~
- [x] ~~Build and flash successfully~~
- [x] ~~Commit Phase 2 infrastructure~~
- [ ] Create mpconfigboard.h with board-specific settings
- [ ] Update micropython_runner.c with full mp_init()/gc_init()
- [ ] Build MicroPython static library (libmicropython.a)
- [ ] Link MicroPython library into component
- [ ] Test basic Python execution (print("Hello"))

### Medium Priority (After VM Works)
- [ ] Create modbadge.c native module skeleton
- [ ] Implement badge.display.* functions (connect to bridge)
- [ ] Implement badge.leds.* functions (connect to bridge)
- [ ] Implement badge.buttons.* functions (connect to bridge)
- [ ] Connect CPU0 display_task() to mp_bridge_recv_display_cmd()
- [ ] Connect CPU0 button ISRs to mp_bridge_send_button_event()
- [ ] Test round-trip: Python → display command → CPU0 renders
- [ ] Test round-trip: Button press → CPU0 → Python receives event

### Lower Priority (Polish)
- [ ] Implement micropython_load_app() with file reading
- [ ] Handle Python exceptions gracefully
- [ ] Return to menu when app exits
- [ ] REPL integration over UART
- [ ] Badge settings API (save/load preferences)
- [ ] App discovery (list apps in /pyapps/)
- [ ] Sample Python apps collection

---

## 🏗️ Build System Notes

### Current MicroPython Integration Challenge
MicroPython is a complex build system that expects to be built standalone. We need to:

1. **Cross-Compile**: Build `mpy-cross` first (runs on host)
2. **Core Library**: Compile MicroPython core (`py/*.c`) into `libmicropython.a`
3. **Port Files**: Compile ESP32-specific glue code
4. **Link**: Link our component against the static library

### Alternative Approaches
- **Option A (Current Plan)**: Build MicroPython as separate library, link into component
- **Option B**: Use MicroPython as IDF component directly (requires restructuring)
- **Option C**: Embed MicroPython source into our component (simpler but less maintainable)

I recommend **Option A** for clean separation.

---

## 📊 Memory Budget

### Current Usage
- Firmware: 379 KB
- Free in partition: 3.6 MB (91%)
- MicroPython heap allocation: 128 KB (configured in mpconfigport.h)
- MP task stack: 16 KB

### Available
- SRAM: ~512 KB total (dual-core ESP32-S3)
- PSRAM: 8 MB (currently disabled, can re-enable later)
- Flash for apps: 1 MB `/pyapps` partition

### Recommendations
- Keep Python heap at 128KB initially
- If memory pressure, enable PSRAM for Python heap
- Python apps limited to ~1 MB total (plenty for badge apps)

---

## 🎯 Success Criteria for Phase 3

Phase 3 will be complete when:

1. ✅ MicroPython VM initializes without errors
2. ✅ Can execute `print("Hello World")` from Python
3. ✅ Can import `badge` module
4. ✅ Python can send display command to CPU0
5. ✅ CPU0 successfully renders Python's display commands
6. ✅ Button press on CPU0 triggers Python event
7. ✅ Python can control LEDs via bridge
8. ✅ Sample app loads from `/pyapps/test/app.py`

---

## 🐛 Potential Issues & Solutions

### Issue: MicroPython Build Complexity
**Solution**: Create standalone Makefile to build libmicropython.a, integrate as prebuilt library

### Issue: Missing mpconfigboard.h
**Solution**: Create minimal board config specific to badge (already done as mpconfigport.h)

### Issue: Threading/GIL Conflicts
**Solution**: Disabled threading in mpconfigport.h (`MICROPY_PY_THREAD = 0`)

### Issue: PSRAM Disabled
**Solution**: Not needed yet - 128KB internal RAM heap is sufficient for badge apps

### Issue: Flash Octal Mode Conflicts
**Solution**: Confirmed working without PSRAM enabled

### Issue: CPU1 Isolation
**Solution**: Use FreeRTOS queues for communication - no shared memory issues

---

## 📝 Developer Notes

### Key Design Decisions

1. **Dual-Core Architecture**: CPU0 = FreeRTOS tasks, CPU1 = MicroPython VM
   - **Rationale**: Isolates Python execution, prevents blocking hardware tasks

2. **Queue-Based Bridge**: FreeRTOS queues for cross-core communication
   - **Rationale**: Thread-safe, non-blocking, proven pattern in ESP-IDF

3. **Minimal MicroPython Config**: Disabled networking, threading, Bluetooth
   - **Rationale**: Badge doesn't need these features - reduces memory/complexity

4. **FAT Filesystem for Apps**: `/pyapps` partition with wear levelling
   - **Rationale**: Easy to modify apps over USB, familiar Python workflow

5. **Native Module Only**: `badge` module implemented in C, not pure Python
   - **Rationale**: Direct hardware access, better performance

### Testing Strategy

Phase 3 testing progression:
1. VM init test (no Python code)
2. Simple print() test
3. Module import test
4. Display command test (stub)
5. Full round-trip test
6. Load external app test

### Documentation to Write

After Phase 3 completion:
- `docs/PYTHON_API.md` - Badge module API reference
- `docs/APP_DEVELOPMENT.md` - Guide for Python app authors
- `pyapps/examples/` - Sample apps collection
- `README_MICROPYTHON.md` - Integration overview

---

## 🎬 Next Session Start Point

**Resume with**: Creating minimal `mpconfigboard.h` and updating `micropython_runner.c` with actual `mp_init()` calls. The infrastructure is ready - we just need to wire up the MicroPython interpreter itself.

**Command to run**:
```bash
cd /home/llatva/git/d26badge-freertos-fw/FreeRTOS
# Review mpconfigport.h
# Update micropython_runner.c with MP VM init
# Attempt build (expect linker errors for missing libmicropython.a)
# Then tackle MicroPython library build separately
```

**Estimated time to Phase 3 completion**: 2-4 hours of focused work

---

*Status saved: 2025-01-XX*
*Last commit: 827fd2b - "Phase 2: Add MicroPython runner infrastructure"*
*Current branch: main*
