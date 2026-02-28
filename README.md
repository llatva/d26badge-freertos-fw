# Disobey Badge 2025/26 – FreeRTOS Firmware by hzb

Custom ESP-IDF / FreeRTOS firmware for the **Disobey 2025 Badge** (ESP32-S3). Features an icon-grid main menu, customisable nickname with accent colours, 12 LED animation modes, three built-in games, WiFi diagnostics, an audio spectrum analyser, an embedded MicroPython demo, and a real-time clock with date/time setting.

Navigate with the D-pad, confirm with **A**, go back with **B**. The idle screen shows your nickname and current time.

---

## Table of Contents

1. [Hardware Summary](#hardware-summary)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
4. [Menu Structure](#menu-structure)
5. [Features](#features)
6. [Building](#building)
7. [Flashing](#flashing)
8. [Serial Monitor](#serial-monitor)
9. [Design Decisions](#design-decisions)

---

## Hardware Summary

| Peripheral    | Interface | Key Pins                                   |
| ------------- | --------- | ------------------------------------------ |
| ST7789 1.9"   | SPI       | SCK=4, MOSI=5, CS=6, DC=15, RST=7, BL=19  |
| SK6812MINI×12 | RMT       | Data=18, Enable=17                         |
| D-pad Up      | GPIO      | 11 (PULL_UP, active-low)                   |
| D-pad Down    | GPIO      | 1  (PULL_UP, active-low)                   |
| D-pad Left    | GPIO      | 21 (PULL_UP, active-low)                   |
| D-pad Right   | GPIO      | 2  (PULL_UP, active-low)                   |
| Stick press   | GPIO      | 14 (PULL_UP, active-low)                   |
| Button A      | GPIO      | 13 (PULL_UP, active-low)                   |
| Button B      | GPIO      | 38 (PULL_UP, active-low)                   |
| Start         | GPIO      | 12 (PULL_UP, active-low)                   |
| Select        | GPIO      | 45 (PULL_DOWN, active-high)                |

---

## Project Structure

```
├── CMakeLists.txt              # Top-level ESP-IDF project file
├── sdkconfig.defaults          # Non-interactive Kconfig defaults
├── Makefile                    # idf.py convenience wrappers
├── setup_idf.sh                # Sources esp-idf/export.sh + sets IDF_TARGET
├── README.md                   # This document
│
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # app_main, FreeRTOS tasks, menu wiring, all screens
│
├── components/
│   ├── st7789/                 # ST7789 SPI display driver + 8×16 font + bitmap drawing
│   ├── sk6812/                 # SK6812 LED driver (12 LEDs, ESP-IDF RMT new API)
│   ├── buttons/                # GPIO interrupt + debounce driver (9 buttons)
│   ├── audio/                  # I2S microphone + audio spectrum analyser
│   ├── menu_ui/                # Menu renderer (list mode + icon grid mode) + icons
│   ├── ui/                     # Screen components:
│   │   ├── idle_screen         #   Idle screen (nickname + clock)
│   │   ├── text_input_screen   #   On-screen text input (nickname editor)
│   │   ├── color_select_screen #   HSV colour picker (accent & text colours)
│   │   ├── about_screen        #   About / version info
│   │   ├── ui_test_screen      #   Hardware diagnostics (display, LEDs, buttons)
│   │   ├── sensor_readout_screen # On-chip sensor readout
│   │   ├── signal_strength_screen # WiFi signal strength meter
│   │   ├── wlan_spectrum_screen   # WiFi channel spectrum analyser
│   │   └── wlan_list_screen       # WiFi networks scanner / list
│   ├── games/                  # Built-in games:
│   │   ├── hacky_bird          #   Flappy Bird clone
│   │   ├── space_shooter       #   Vertical space shooter
│   │   └── snake               #   Classic Snake
│   ├── micropython_runner/     # On-demand MicroPython VM (v1.27.0)
│   └── pyapps_fs/              # Python apps filesystem support
│
├── esp-idf/                    # ESP-IDF v5.5 (git submodule)
├── micropython/                # MicroPython v1.27.0 (git submodule)
└── FreeRTOS/                   # FreeRTOS kernel (git submodule)
```

---

## Architecture

```
                 ┌──────────────────────────────────────────────────────┐
                 │  CPU0 (PRO_CPU)                                     │
                 │                                                     │
  buttons ISR ──►│  g_btn_queue ──► input_task                         │
                 │                      │  menu navigation / actions   │
                 │                      │  g_disp_queue ─► display_task│
                 │                                             │       │
                 │                                      screen drawing │
                 │                                             │       │
                 │                                          ST7789     │
                 │                                                     │
                 │  led_task ◄── g_led_mode (atomic_int)               │
                 │      │                                              │
                 │   SK6812 ×12                                        │
                 │                                                     │
                 │  python_demo_task (spawned on demand, 32 KB stack)   │
                 │      │                                              │
                 │   MicroPython VM (32 KB heap per invocation)        │
                 └──────────────────────────────────────────────────────┘
```

### Tasks

| Task               | Priority | Stack   | Role                                        |
| ------------------ | -------- | ------- | ------------------------------------------- |
| `input_task`       | 6        | 2 KB    | Reads button queue, drives menu & screens   |
| `display_task`     | 5        | 4 KB    | Owns SPI bus; draws menus & screen states   |
| `led_task`         | 4        | 4 KB    | Polls `g_led_mode`; animates SK6812 LEDs    |
| `python_demo_task` | 5        | 32 KB   | On-demand; runs MicroPython demos           |

### Application States

The firmware uses an `app_state_t` enum to manage which screen is active. The `input_task` and `display_task` switch behaviour based on the current state:

| State                | Screen                           |
| -------------------- | -------------------------------- |
| `APP_STATE_IDLE`     | Nickname + clock display         |
| `APP_STATE_MENU`     | Icon grid / list menu            |
| `APP_STATE_SETTINGS` | Nickname text editor             |
| `APP_STATE_COLOR_SELECT` | HSV accent colour picker     |
| `APP_STATE_TIME_DATE_SET` | Time & date editor            |
| `APP_STATE_AUDIO_SPECTRUM` | Audio spectrum analyser     |
| `APP_STATE_HACKY_BIRD` | Hacky Bird game                |
| `APP_STATE_SPACE_SHOOTER` | Space Shooter game           |
| `APP_STATE_SNAKE`    | Snake game                       |
| `APP_STATE_PYTHON_DEMO` | Interactive MicroPython demos |
| `APP_STATE_UI_TEST`  | Hardware diagnostics             |
| `APP_STATE_WLAN_SPECTRUM` | WiFi channel spectrum        |
| `APP_STATE_WLAN_LIST` | WiFi networks scanner           |
| `APP_STATE_ABOUT`    | Firmware version & info          |

---

## Menu Structure

The root menu uses a **2×3 icon grid** with hand-designed 24×24 monochrome bitmap icons:

| Icon | Menu | Contents |
| ---- | ---- | -------- |
| 🔧 | **Tools** | Audio Spectrum Analyser |
| 🎮 | **Games** | Hacky Bird, Space Shooter, Snake |
| ⚙️ | **Settings** | Edit Nickname, Accent Color, Text Color, LED Animation (submenu), Set Time & Date |
| 📊 | **Diagnostics** | UI Test, Sensor Readout, Signal Strength, WiFi Spectrum, WiFi Networks |
| 💻 | **Development** | Python Demo |
| ❓ | **About** | Firmware version, badge info |

### LED Animations (Settings → LED Animation)

| Mode | Effect |
| ---- | ------ |
| Accent Pulse | Breathing with user accent colour |
| Rainbow | Rotating rainbow cycle |
| Disco Party | Fast random colours |
| Police Strobe | Red/blue strobe on sides |
| Smooth Relax | Slow smooth colour morphing |
| Smooth Rotate | Colour rotating around the frame |
| LED Chase | Single lit LED chasing |
| Color Morph | Slow morph between colours |
| Breath Cycle | Breathing while colour cycling |
| Disobey Identity | DISOBEY colour wheel |
| Flame | Simulated flames on sides |
| VU Meter | Microphone-driven VU meter |
| Off | All LEDs off |

---

## Features

### Idle Screen
Displays the user's **nickname** (scale 4, centred) and the current **date/time**. Press any button to enter the menu.

### Nickname & Colours
- **Edit Nickname**: On-screen keyboard with fast cycling (hold UP/DOWN), up to 10 characters
- **Accent Colour**: HSV colour picker for LED accent and UI highlights
- **Text Colour**: Separate colour for nickname text on the idle screen

### Time & Date Setting
Interactive editor with 5 fields (Hour, Minute, Year, Month, Day). LEFT/RIGHT moves between fields, UP/DOWN adjusts values with proper wrapping and leap year handling. A/START confirms via `settimeofday()`.

### Games
- **Hacky Bird** – Flappy Bird clone with score tracking
- **Space Shooter** – Vertical scrolling space shooter
- **Snake** – Classic Snake game

### MicroPython Demo
Six interactive Python demos running on the embedded MicroPython v1.27.0 VM with real stdout capture:

| Demo | Description |
| ---- | ----------- |
| Fibonacci | Recursive vs iterative timing comparison |
| Prime Sieve | Eratosthenes sieve + twin primes |
| Classes & OOP | Vector/Particle simulation |
| Generators | Collatz conjecture, map/filter |
| Mandelbrot | 38×9 ASCII art fractal |
| Badge Info | System info, memory stats, feature detection |

Navigate with LEFT/RIGHT, scroll with UP/DOWN, exit with B. Each demo lights the LEDs in a unique colour theme.

### WiFi Diagnostics
- **WiFi Spectrum** – Real-time channel utilisation analyser
- **WiFi Networks** – Scans and lists nearby access points with signal strength
- **Signal Strength** – RSSI meter for the connected network

### Audio Spectrum
Real-time FFT audio spectrum analyser using the on-board I2S microphone.

### Hardware Diagnostics (UI Test)
Colour bars, LED rainbow test, and button-press verification. Exit with B+START.

---

## Building

### Prerequisites

- ESP-IDF v5.5 (included as `esp-idf/` git submodule)
- MicroPython v1.27.0 (included as `micropython/` git submodule)

### First-Time Setup

```bash
# 1. Initialise git submodules
make submodules

# 2. Install ESP-IDF toolchain (only needed once)
make idf_install

# 3. Activate the ESP-IDF environment (needed in each terminal session)
source setup_idf.sh
```

### Build

```bash
source setup_idf.sh   # if not already done in this terminal
idf.py build           # or: make build
```

The first build generates `sdkconfig` from `sdkconfig.defaults` and takes a few minutes. Subsequent builds are incremental.

---

## Flashing

```bash
# Auto-detect USB port
make flash

# Explicit port
make flash PORT=/dev/ttyUSB0
```

---

## Serial Monitor

```bash
make monitor PORT=/dev/ttyUSB0

# Flash + open monitor in one step
make flash_monitor PORT=/dev/ttyUSB0
```

Default baud rate is 115 200 (USB Serial JTAG console).

---

## Design Decisions

| Decision | Rationale |
| -------- | --------- |
| All tasks on CPU0 | Keeps CPU1 available for future dedicated MicroPython task |
| On-demand MicroPython VM | Avoids boot-time heap conflicts; VM is initialised only when a demo runs |
| 32 KB MicroPython heap | Enough for demo scripts; allocated/freed per invocation |
| Polling SPI (no DMA interrupt) | Simplifies ownership — `display_task` owns the bus exclusively |
| `atomic_int` for LED mode | Cheapest cross-task signalling; single-word writes |
| 20 ms ISR debounce timer | Matches mechanical button bounce; avoids polling overhead |
| RMT new API (IDF 5.x) | `rmt_new_bytes_encoder` is the correct API for IDF 5.5 |
| Row-offset 35 for ST7789 | The 1.9" panel is a 320×170 window inside a 320×240 controller |
| SK6812 GRB byte order | SK6812 uses G-R-B on the wire (unlike some WS2812B variants) |
| Icon grid menu | 2×3 grid with 24×24 monochrome bitmaps for visual navigation |
