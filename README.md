# Disobey Badge 2025 – FreeRTOS 3rdparty Firmware by hzb

An ESP-IDF / FreeRTOS firmware for the **Disobey 2025 Badge** that shows an interactive LED-control menu on the built-in ST7789 display.  Navigate with the D-pad, confirm with **A**, **STICK press**, or **SELECT**, and instantly see the chosen LED effect on the 8 × SK6812MINI LEDs.

> **CPU partitioning**: all tasks run on **CPU0** (`PRO_CPU`). **CPU1** (`APP_CPU`) is intentionally left idle so a future MicroPython VM can be spawned there independently.

> **🐍 NEW: MicroPython Integration!** See [`MICROPYTHON_INTEGRATION_PLAN.md`](MICROPYTHON_INTEGRATION_PLAN.md) for the roadmap to add user-extensible Python mini-apps. Quick start: [`QUICKSTART_MICROPYTHON.md`](QUICKSTART_MICROPYTHON.md)

---

## Table of Contents

1. [Hardware Summary](#hardware-summary)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
4. [Menu Items & LED Modes](#menu-items--led-modes)
5. [Building](#building)
6. [Flashing](#flashing)
7. [Serial Monitor](#serial-monitor)
8. [Adding a New LED Mode](#adding-a-new-led-mode)
9. [Design Decisions](#design-decisions)
10. [**MicroPython Apps** (Coming Soon)](#micropython-apps)

---

## Hardware Summary

| Peripheral   | Interface | Key Pins                                   |
| ------------ | --------- | ------------------------------------------ |
| ST7789 1.9"  | SPI       | SCK=4, MOSI=5, CS=6, DC=15, RST=7, BL=19  |
| SK6812MINI×8 | RMT       | Data=18, Enable=17                         |
| D-pad Up     | GPIO      | 11 (PULL_UP, active-low)                   |
| D-pad Down   | GPIO      | 1  (PULL_UP, active-low)                   |
| D-pad Left   | GPIO      | 21 (PULL_UP, active-low)                   |
| D-pad Right  | GPIO      | 2  (PULL_UP, active-low)                   |
| Stick press  | GPIO      | 14 (PULL_UP, active-low)                   |
| Button A     | GPIO      | 13 (PULL_UP, active-low)                   |
| Button B     | GPIO      | 38 (PULL_UP, active-low)                   |
| Start        | GPIO      | 12 (PULL_UP, active-low)                   |
| Select       | GPIO      | 45 (PULL_DOWN, active-high)                |

Full details: [../HARDWARE.md](../HARDWARE.md)

---

## Project Structure

```
FreeRTOS/
├── CMakeLists.txt              # Top-level ESP-IDF project file
├── sdkconfig.defaults          # Non-interactive Kconfig defaults
├── Makefile                    # idf.py convenience wrappers
├── setup_idf.sh                # Sources parent set_environ.sh + sets IDF_TARGET
├── README.md                   # This document
│
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # app_main, FreeRTOS tasks, menu wiring
│
└── components/
    ├── st7789/                 # ST7789 SPI display driver + 8×16 font
    │   ├── CMakeLists.txt
    │   ├── include/st7789.h
    │   ├── st7789.c
    │   └── font8x16.h
    │
    ├── sk6812/                 # SK6812 LED driver (ESP-IDF RMT new API)
    │   ├── CMakeLists.txt
    │   ├── include/sk6812.h
    │   └── sk6812.c
    │
    ├── buttons/                # GPIO interrupt + debounce driver
    │   ├── CMakeLists.txt
    │   ├── include/buttons.h
    │   └── buttons.c
    │
    └── menu_ui/                # Framebuffer menu renderer
        ├── CMakeLists.txt
        ├── include/menu_ui.h
        └── menu_ui.c
```

---

## Architecture

```
                 ┌─────────────────────────────────────────────────────┐
                 │  CPU0 (PRO_CPU)                                      │
                 │                                                       │
  buttons ISR ──►│  g_btn_queue ──► input_task                          │
                 │                      │  menu_navigate_up/down        │
                 │                      │  menu_select                  │
                 │                      │  g_disp_queue ─► display_task │
                 │                                             │         │
                 │                                      menu_draw()     │
                 │                                             │         │
                 │                                          ST7789       │
                 │                                                       │
                 │  led_task ◄── g_led_mode (atomic_int)                │
                 │      │                                                │
                 │   SK6812                                              │
                 └─────────────────────────────────────────────────────┘
                 ┌─────────────────────────────────────────────────────┐
                 │  CPU1 (APP_CPU) – RESERVED for MicroPython VM        │
                 └─────────────────────────────────────────────────────┘
```

### Tasks

| Task          | Priority | Stack  | Role                                       |
| ------------- | -------- | ------ | ------------------------------------------ |
| `input_task`  | 6        | 2 kB   | Reads `g_btn_queue`, drives menu navigation |
| `display_task`| 5        | 4 kB   | Owns SPI bus; draws menu on `g_disp_queue` |
| `led_task`    | 4        | 2 kB   | Polls `g_led_mode`; animates SK6812 LEDs   |

### Queues & Shared State

| Object          | Type           | Producer       | Consumer       |
| --------------- | -------------- | -------------- | -------------- |
| `g_btn_queue`   | `btn_event_t`  | buttons ISR    | `input_task`   |
| `g_disp_queue`  | `disp_cmd_t`   | `input_task`   | `display_task` |
| `g_led_mode`    | `atomic_int`   | menu callbacks | `led_task`     |

---

## Menu Items & LED Modes

| # | Label           | LED Effect                                      | Keys to activate          |
| - | --------------- | ----------------------------------------------- | ------------------------- |
| 0 | All Off         | All 8 LEDs off                                  | UP/DOWN to select + A     |
| 1 | Red             | All LEDs solid red (dim)                        | UP/DOWN to select + A     |
| 2 | Green           | All LEDs solid green (dim)                      | UP/DOWN to select + A     |
| 3 | Blue            | All LEDs solid blue (dim)                       | UP/DOWN to select + A     |
| 4 | Rainbow         | Rotating rainbow cycle                          | UP/DOWN to select + A     |
| 5 | Badge Identity  | Alternating magenta/white breathing animation   | UP/DOWN to select + A     |

**Confirm / activate**: press **A**, **joystick stick**, or **SELECT**.

---

## Building

### Prerequisites

The FreeRTOS firmware uses ESP-IDF 5.5.x located at `esp-idf/` in the repo root.

#### First-Time Setup

1. **Initialize git submodules** (if not already done):

   ```bash
   make submodules
   ```

2. **Install ESP-IDF toolchain and Python environment**:

   ```bash
   make idf_install
   ```

   This runs `esp-idf/install.sh esp32s3` which:
   - Downloads and installs the Xtensa toolchain for ESP32-S3
   - Creates a Python virtual environment at `~/.espressif/python_env/idf5.5_py3.12_env/`
   - Installs all required Python packages (esptool, idf-component-manager, etc.)

   **Note**: This only needs to be done once, or when updating ESP-IDF versions.

3. **Activate the ESP-IDF environment**:

   ```bash
   source FreeRTOS/setup_idf.sh
   ```

   This script:
   - Searches for `esp-idf/export.sh` in multiple locations (system-wide `/esp-idf/`, repo-local `esp-idf/`, or `micropython/esp-idf/`)
   - Sources the export script to put `idf.py` and the Xtensa toolchain on `PATH`
   - Sets badge-specific environment variables (board type, target, etc.)

   **You must source this script in each new terminal session** before building.

### Building the Firmware

1. From the **repo root**, source the FreeRTOS environment script:

   ```bash
   source FreeRTOS/setup_idf.sh
   ```

   This sources `esp-idf/export.sh` which puts `idf.py` and the Xtensa toolchain on `PATH`.

2. Build from the repo root:

   ```bash
   make build
   ```

   Or build directly from the `FreeRTOS/` directory:

   ```bash
   cd FreeRTOS
   idf.py build
   ```

### First build

The first build generates `FreeRTOS/sdkconfig` from `sdkconfig.defaults`.  This takes a few minutes.  Subsequent builds are incremental.

---

## Flashing

```bash
# Auto-detect USB port
make flash

# Explicit port
make flash PORT=/dev/ttyUSB0

# Or from FreeRTOS/ directory
cd FreeRTOS && idf.py -p /dev/ttyUSB0 flash
```

---

## Serial Monitor

```bash
make monitor PORT=/dev/ttyUSB0

# Flash + open monitor in one step
make flash_monitor PORT=/dev/ttyUSB0
```

Default baud rate is 115 200 (set in `sdkconfig.defaults` via `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`).

---

## Adding a New LED Mode

1. Add a new `LED_MODE_xxx` value to the `led_mode_t` enum in `main/main.c`.
2. Add a static action callback:
   ```c
   static void action_led_xxx(void) {
       atomic_store(&g_led_mode, LED_MODE_XXX);
   }
   ```
3. Add a new `switch` case in `led_task()`.
4. Register it in `app_main()`:
   ```c
   menu_add_item(&g_menu, "My Effect", action_led_xxx);
   ```
5. Build and flash.

---

## Design Decisions

| Decision | Rationale |
| -------- | --------- |
| All tasks on CPU0 | Leaves CPU1 fully available for a future MicroPython VM without repartitioning |
| Polling SPI (no DMA interrupt) | Simplifies ownership model — `display_task` owns the bus exclusively |
| `atomic_int` for `g_led_mode` | Cheapest cross-task signalling; LED mode updates are single-word writes |
| 20 ms ISR debounce timer | Matches typical mechanical button bounce; avoids polling overhead |
| RMT new API (IDF 5.x) | `rmt_new_bytes_encoder` is the correct API for IDF 5.2.2; old API removed |

---

## MicroPython Apps

**Status:** 🚧 Planning Phase (Implementation starting soon)

The badge firmware is being extended to support **user-created Python mini-apps** that run on CPU1 alongside the main FreeRTOS firmware.

### Vision

- Write mini-apps in Python (games, tools, animations)
- Upload via USB serial or WiFi
- Access badge hardware through clean Python API
- Apps appear in menu automatically
- Example: Snake game included

### Documentation

- **Full Plan:** [`MICROPYTHON_INTEGRATION_PLAN.md`](MICROPYTHON_INTEGRATION_PLAN.md) - Complete architecture, phases, timeline
- **Quick Start:** [`QUICKSTART_MICROPYTHON.md`](QUICKSTART_MICROPYTHON.md) - Start implementing Phase 1 today
- **Partition Table:** [`partitions_ota_micropython.csv`](partitions_ota_micropython.csv) - New layout with OTA + Python apps

### Architecture Preview

```
┌─────────────────────────────────────────┐
│  ESP32-S3 (Dual Core)                   │
│  ┌─────────────┐  ┌──────────────────┐ │
│  │ CPU0        │  │ CPU1             │ │
│  │ FreeRTOS    │◄─┤ MicroPython VM   │ │
│  │ - Display   │  │ - User apps      │ │
│  │ - LEDs      │  │ - Snake game     │ │
│  │ - Buttons   │  │ - Custom tools   │ │
│  └─────────────┘  └──────────────────┘ │
└─────────────────────────────────────────┘
```

### Example: Snake Game (Planned)

```python
import badge

# Draw to display
badge.display.clear()
badge.display.text("Score: 42", 10, 10)

# Control LEDs
badge.leds.fill(r=0, g=255, b=0)

# Read buttons
if badge.buttons.get()['up']:
    # Move snake up
    pass

badge.display.show()
```

### Timeline

- **Phase 1-2:** Partition table + filesystem (2-3 weeks)
- **Phase 3-4:** API bridge + app launcher (2 weeks)
- **Phase 5:** OTA system (1 week)
- **Phase 6:** Dev tools + REPL (3 days)

**Total:** ~6 weeks to production-ready

### Contributing

Want to help? See the implementation plan for:
- Component architecture
- API design
- Example apps
- Testing strategy

**Questions?** Open an issue or discussion!
| Row-offset 35 for ST7789 | The ER-TFT019-1 1.9" panel is a 320×170 window inside a 320×240 controller |
| SK6812 GRB byte order | SK6812 (unlike some WS2812B variants) uses G-R-B on the wire |
