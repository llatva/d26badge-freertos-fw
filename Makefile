##############################################################################
# FreeRTOS/Makefile
#
# Convenience wrappers around `idf.py` for building and flashing the
# Disobey Badge 2025 FreeRTOS firmware.
#
# Prerequisites:
#   - ESP-IDF 5.2.2 sourced (run `source setup_idf.sh` first, or
#     `source ../set_environ.sh` from inside this directory).
#   - `idf.py` on PATH (provided by IDF environment).
#
# Usage:
#   make build          – configure + build
#   make flash          – build + flash via USB-Serial (PORT auto-detected)
#   make monitor        – open serial monitor
#   make flash_monitor  – flash then open monitor
#   make clean          – remove build directory
#   make menuconfig     – open Kconfig UI
#   make size           – show firmware size analysis
#   make help           – print this help
#
# Optional overrides (via environment or make arg):
#   PORT=/dev/ttyUSBx  – override serial port
#   BAUD=921600        – override flash baud rate (default 460800)
#   IDF_PY=idf.py      – idf.py binary path
##############################################################################

.PHONY: build flash monitor flash_monitor \
        clean menuconfig size help idf_install

FRTOS_DIR     := $(dir $(abspath $(filter %FreeRTOS/Makefile %FreeRTOS%Makefile, $(MAKEFILE_LIST))))
FRTOS_DIR     := $(if $(FRTOS_DIR),$(FRTOS_DIR),$(CURDIR)/FreeRTOS/)
REPO_ROOT     := $(abspath $(FRTOS_DIR)/..)
IDF_PY        ?= idf.py
FRTOS_BAUD    ?= 460800

# Port detection: honour PORT env var, else let idf.py auto-detect
ifdef PORT
  FRTOS_PORT_ARG := -p $(PORT)
else
  FRTOS_PORT_ARG :=
endif

# All idf.py commands run from the FreeRTOS directory
IDF := $(IDF_PY)

build:
	@echo "=== Building FreeRTOS badge firmware ==="
	$(IDF) build

flash: build
	@echo "=== Flashing FreeRTOS firmware ==="
	$(IDF) $(FRTOS_PORT_ARG) -b $(FRTOS_BAUD) flash

monitor:
	@echo "=== Opening serial monitor ==="
	$(IDF) $(FRTOS_PORT_ARG) monitor

flash_monitor: build
	@echo "=== Flashing and monitoring ==="
	$(IDF) $(FRTOS_PORT_ARG) -b $(FRTOS_BAUD) flash monitor

clean:
	@echo "=== Cleaning FreeRTOS build ==="
	$(IDF) fullclean

menuconfig:
	$(IDF) menuconfig

size: build
	$(IDF) size

help:
	@echo "FreeRTOS firmware targets:"
	@echo "  idf_install      Install ESP-IDF toolchain and Python environment"
	@echo "  build            Build the firmware"
	@echo "  flash            Build + flash (PORT=... optional)"
	@echo "  monitor          Open serial monitor"
	@echo "  flash_monitor    Flash then monitor"
	@echo "  clean            Remove build output"
	@echo "  menuconfig       Open Kconfig UI"
	@echo "  size             Show firmware size"
	@echo ""
	@echo "Examples:"
	@echo "  make idf_install"
	@echo "  make build"
	@echo "  make flash PORT=/dev/ttyUSB0"
	@echo "  make flash_monitor PORT=/dev/ttyUSB0"

idf_install:
	@echo "=== Installing ESP-IDF toolchain and Python environment ==="
	@if [ ! -d "$(REPO_ROOT)/esp-idf" ]; then \
		echo "ERROR: esp-idf directory not found at $(REPO_ROOT)/esp-idf"; \
		echo "Please run 'make submodules' from the repo root first."; \
		exit 1; \
	fi
	cd $(REPO_ROOT)/esp-idf && ./install.sh esp32s3
	@echo ""
	@echo "=== ESP-IDF installation complete ==="
	@echo "To activate the environment, run:"
	@echo "  source FreeRTOS/setup_idf.sh"
