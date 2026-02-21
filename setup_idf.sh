#!/usr/bin/env bash
# FreeRTOS/setup_idf.sh
#
# Activates ESP-IDF for the FreeRTOS sub-project.
# Must be *sourced*, not executed:
#
#   source FreeRTOS/setup_idf.sh
#
# This script:
#   1. Exports common badge build variables (BOARD, BAUD, PORT, etc.)
#      from the parent set_environ.sh.
#   2. Sources micropython/esp-idf/export.sh to put idf.py on PATH.
#
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Check for ESP-IDF in multiple locations (system-wide, repo, or micropython submodule)
IDF_EXPORT=""
if [ -f "/esp-idf/export.sh" ]; then
    IDF_EXPORT="/esp-idf/export.sh"
elif [ -f "${REPO_ROOT}/esp-idf/export.sh" ]; then
    IDF_EXPORT="${REPO_ROOT}/esp-idf/export.sh"
elif [ -f "${REPO_ROOT}/micropython/esp-idf/export.sh" ]; then
    IDF_EXPORT="${REPO_ROOT}/micropython/esp-idf/export.sh"
fi

if [ -z "${IDF_EXPORT}" ]; then
    echo "ERROR: ESP-IDF export.sh not found. Searched:"
    echo "  /esp-idf/export.sh"
    echo "  ${REPO_ROOT}/esp-idf/export.sh"
    echo "  ${REPO_ROOT}/micropython/esp-idf/export.sh"
    echo "Please ensure ESP-IDF is installed at one of these locations."
    return 1
fi

# Export badge-specific variables from parent set_environ.sh
export IDF_CCACHE_ENABLE=1
export BOARD=ESP32_GENERIC_S3
export BOARD_VARIANT=DEVKITW2
export BAUD=115200
export IDF_TARGET=esp32s3

# Activate ESP-IDF (adds idf.py and xtensa toolchain to PATH)
source "${IDF_EXPORT}"

echo "ESP-IDF ready for FreeRTOS sub-project (target: ${IDF_TARGET}, board: ${BOARD})"
