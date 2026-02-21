# MicroPython Integration - Implementation Checklist

Track your progress as you implement the MicroPython integration. Check off items as you complete them.

## Phase 1: Foundation ⏳

### 1.1 Partition Table & Filesystem
- [ ] Created `partitions_ota_micropython.csv`
- [ ] Updated `sdkconfig.defaults` with custom partition config
- [ ] Added FAT filesystem config options
- [ ] Built firmware successfully with new partition table
- [ ] Erased flash and flashed new partition table
- [ ] Verified boot logs show correct partition layout

### 1.2 Filesystem Component
- [ ] Created `components/pyapps_fs/` directory
- [ ] Implemented `pyapps_fs.h` header
- [ ] Implemented `pyapps_fs.c` with mount/unmount functions
- [ ] Created component `CMakeLists.txt`
- [ ] Integrated into `main.c`
- [ ] Successfully mounted `/pyapps` partition
- [ ] Tested file read/write to `/pyapps/test.txt`

### 1.3 MicroPython Submodule
- [ ] Added MicroPython as git submodule
- [ ] Checked out stable version (v1.23.0 or later)
- [ ] Built `mpy-cross` compiler successfully
- [ ] Initialized ESP32 port submodules

---

## Phase 2: API Bridge 🔌

### 2.1 MicroPython Runner Component
- [ ] Created `components/micropython_runner/` directory
- [ ] Implemented `micropython_task.c` (spawns VM on CPU1)
- [ ] Allocated MicroPython heap (256KB)
- [ ] MicroPython VM starts and runs REPL
- [ ] Verified CPU1 task affinity

### 2.2 Badge Native Module
- [ ] Created `modbadge.c` skeleton
- [ ] Implemented display API:
  - [ ] `badge.display.clear()`
  - [ ] `badge.display.text()`
  - [ ] `badge.display.rect()`
  - [ ] `badge.display.show()`
- [ ] Implemented LED API:
  - [ ] `badge.leds.set()`
  - [ ] `badge.leds.fill()`
  - [ ] `badge.leds.show()`
- [ ] Implemented button API:
  - [ ] `badge.buttons.get()`
  - [ ] `badge.buttons.wait()`
- [ ] Implemented control API:
  - [ ] `badge.exit()`

### 2.3 Inter-Core Communication
- [ ] Created `mp_display_queue` (Python → CPU0)
- [ ] Created `mp_button_queue` (CPU0 → Python)
- [ ] Added display lock semaphore
- [ ] Integrated queue handling in FreeRTOS tasks
- [ ] Tested Python → C command flow
- [ ] Tested C → Python event flow

---

## Phase 3: App Discovery & Launcher 🚀

### 3.1 Filesystem Structure
- [ ] Defined `/pyapps/` directory structure
- [ ] Created manifest.json schema
- [ ] Documented app.py requirements

### 3.2 App Discovery
- [ ] Implemented `scan_pyapps()` in C
- [ ] Directory scanning works
- [ ] manifest.json parsing works
- [ ] Populated `g_pyapp_list[]` array

### 3.3 Menu Integration
- [ ] Added "Python Apps" submenu to main menu
- [ ] Apps appear in submenu dynamically
- [ ] Menu items have correct labels from manifests

### 3.4 App Launcher
- [ ] Implemented `launch_pyapp()` in C
- [ ] Command sent to MicroPython task
- [ ] Resource handoff (display/buttons) works
- [ ] FreeRTOS tasks pause correctly during app
- [ ] Python `run_app()` function works
- [ ] App execution completes successfully
- [ ] Clean return to FreeRTOS menu after exit

---

## Phase 4: Example App - Snake Game 🐍

### 4.1 Snake Implementation
- [ ] Created `/pyapps/snake/` directory
- [ ] Implemented `app.py` with Snake class
- [ ] Game logic works (movement, collision, food)
- [ ] Drawing to display works
- [ ] Button input responsive
- [ ] Score tracking works
- [ ] Game over screen displays
- [ ] Clean exit to menu

### 4.2 Manifest & Assets
- [ ] Created `manifest.json`
- [ ] App appears in Python Apps menu
- [ ] Launches from menu
- [ ] Plays smoothly at target FPS

---

## Phase 5: OTA Update System 📦

### 5.1 OTA Infrastructure
- [ ] Created `components/ota_manager/`
- [ ] WiFi connection manager implemented
- [ ] HTTP OTA client works
- [ ] HTTPS OTA client works (optional)
- [ ] Rollback on boot failure works
- [ ] OTA progress displayed on screen

### 5.2 Menu Integration
- [ ] Added "System Update" menu item
- [ ] Update check works
- [ ] Download progress shows
- [ ] Firmware validates successfully
- [ ] Reboot and activation works

### 5.3 Python OTA API (Optional)
- [ ] `badge.ota.check_update()` implemented
- [ ] `badge.ota.download_and_install()` implemented

---

## Phase 6: Development Tools 🛠️

### 6.1 App Upload Tool
- [ ] Created `tools/upload_app.py`
- [ ] Serial connection works
- [ ] Raw REPL mode entry works
- [ ] File upload works
- [ ] Directory upload works
- [ ] Error handling works

### 6.2 REPL Access
- [ ] Added "Python REPL" menu item
- [ ] REPL accessible via serial
- [ ] Commands execute on CPU1
- [ ] Can test `badge` API interactively

---

## Testing & Validation ✅

### Unit Tests
- [ ] Partition mounting test passes
- [ ] App discovery test passes
- [ ] Inter-core messaging latency acceptable (<50ms)
- [ ] Badge API bridge tests pass

### Integration Tests
- [ ] Menu navigation to/from apps works
- [ ] Display ownership transfers correctly
- [ ] Button routing works in both modes
- [ ] LED control works from Python
- [ ] Python exception doesn't crash FreeRTOS
- [ ] Memory usage within budget

### User Acceptance Tests
- [ ] Upload snake game via serial
- [ ] Navigate menu to "Python Apps" → "Snake Game"
- [ ] Play game with D-pad
- [ ] Exit with B button returns to menu
- [ ] All FreeRTOS features still work

---

## Documentation 📚

- [ ] Updated README.md with MicroPython section
- [ ] Created user guide for uploading apps
- [ ] Documented Python API reference
- [ ] Added example apps beyond snake
- [ ] Created troubleshooting guide

---

## Future Enhancements (Post-MVP) 🌟

- [ ] WiFi networking API
- [ ] Web-based app uploader
- [ ] App marketplace integration
- [ ] More example apps (calculator, logger, etc.)
- [ ] Python debugger (WebREPL)
- [ ] Bluetooth API
- [ ] App state persistence

---

## Notes & Issues 📝

Use this section to track blockers, decisions, and learnings:

```
[Date] [Issue/Note]
- 
- 
```

---

**Progress Summary**

- Phase 1: ⬜ Not Started | ⏳ In Progress | ✅ Complete
- Phase 2: ⬜ Not Started | ⏳ In Progress | ✅ Complete
- Phase 3: ⬜ Not Started | ⏳ In Progress | ✅ Complete
- Phase 4: ⬜ Not Started | ⏳ In Progress | ✅ Complete
- Phase 5: ⬜ Not Started | ⏳ In Progress | ✅ Complete
- Phase 6: ⬜ Not Started | ⏳ In Progress | ✅ Complete

**Overall Status:** Planning Phase ✅ | Implementation 🚧

---

**Last Updated:** 2026-02-21  
**Next Review:** After Phase 1 completion
