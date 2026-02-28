# CMakeLists.txt for micropython_runner component
#
# Integrates MicroPython core into an ESP-IDF component using
# MicroPython's own py.cmake + mkrules.cmake for proper qstr,
# moduledefs, and root_pointers generation.

# Derive repo root from component directory (components/micropython_runner -> repo root)
get_filename_component(BADGE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(MICROPY_DIR "${BADGE_REPO_ROOT}/micropython")
set(MICROPY_PY_DIR "${MICROPY_DIR}/py")

# Include MicroPython's source list (sets MICROPY_SOURCE_PY, MICROPY_INC_CORE)
include(${MICROPY_DIR}/py/py.cmake)

# Our own source files
set(RUNNER_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/micropython_runner.c"
    "${CMAKE_CURRENT_LIST_DIR}/mp_bridge.c"
    "${CMAKE_CURRENT_LIST_DIR}/mphalport.c"
    "${CMAKE_CURRENT_LIST_DIR}/modbadge.c"
)

# Shared runtime helpers that MicroPython needs
set(MICROPY_SOURCE_SHARED
    "${MICROPY_DIR}/shared/runtime/interrupt_char.c"
    "${MICROPY_DIR}/shared/runtime/stdout_helpers.c"
    "${MICROPY_DIR}/shared/runtime/sys_stdio_mphal.c"
)

# Port-specific qstr definitions
set(MICROPY_QSTRDEFS_PORT
    "${CMAKE_CURRENT_LIST_DIR}/qstrdefsport.h"
)

# Files to scan for Q() macros (qstr auto-collection)
set(MICROPY_SOURCE_QSTR
    ${MICROPY_SOURCE_PY}
    ${MICROPY_SOURCE_SHARED}
    ${RUNNER_SRCS}
)

# Register the ESP-IDF component
idf_component_register(
    SRCS
        ${RUNNER_SRCS}
        ${MICROPY_SOURCE_PY}
        ${MICROPY_SOURCE_SHARED}
    INCLUDE_DIRS
        "include"
        "."
        "${MICROPY_DIR}"
    REQUIRES
        freertos
        esp_system
        esp_common
        nvs_flash
        heap
        soc
        esp_timer
)

# Set the MicroPython target for mkrules.cmake
set(MICROPY_TARGET ${COMPONENT_LIB})

# Additional include directories for MicroPython headers
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${MICROPY_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}"
)

# The build directory where genhdr/ will be generated
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${CMAKE_BINARY_DIR}"
)

# ─── Build MICROPY_CPP_FLAGS explicitly ───
# We must set MICROPY_CPP_FLAGS before including mkrules.cmake so that
# it does NOT try get_target_property(COMPILE_DEFINITIONS) which returns
# -NOTFOUND when empty.  We also limit the include set to only the
# components MicroPython sources actually reference, keeping the
# preprocessor command line short.

# Start with our own include directories
set(MICROPY_CPP_FLAGS
    -I${PROJECT_DIR}/build/config
    -I${CMAKE_CURRENT_LIST_DIR}/include
    -I${CMAKE_CURRENT_LIST_DIR}
    -I${MICROPY_DIR}
    -I${CMAKE_BINARY_DIR}
)

# Only gather includes from the components we actually depend on
set(_MP_NEEDED_COMPONENTS
    freertos esp_system esp_common nvs_flash heap soc esp_timer
    newlib esp_hw_support esp_rom hal log xtensa esp_event
    esp_driver_gpio driver esp_partition spi_flash
)
foreach(comp ${_MP_NEEDED_COMPONENTS})
    if(TARGET __idf_${comp})
        get_target_property(_inc __idf_${comp} INCLUDE_DIRECTORIES)
        if(_inc)
            foreach(_d ${_inc})
                # Skip generator expressions
                string(FIND "${_d}" "$<" _genexpr_pos)
                if(_genexpr_pos EQUAL -1 AND NOT "${_d}" STREQUAL "")
                    list(APPEND MICROPY_CPP_FLAGS -I${_d})
                endif()
            endforeach()
        endif()
        get_target_property(_inc2 __idf_${comp} INTERFACE_INCLUDE_DIRECTORIES)
        if(_inc2)
            foreach(_d ${_inc2})
                string(FIND "${_d}" "$<" _genexpr_pos)
                if(_genexpr_pos EQUAL -1 AND NOT "${_d}" STREQUAL "")
                    list(APPEND MICROPY_CPP_FLAGS -I${_d})
                endif()
            endforeach()
        endif()
    endif()
endforeach()

# Remove duplicates to shorten the command line
list(REMOVE_DUPLICATES MICROPY_CPP_FLAGS)

# Include MicroPython's mkrules.cmake – this sets up the custom commands
# to auto-generate qstrdefs.generated.h, moduledefs.h, root_pointers.h,
# and mpversion.h at build time.
include(${MICROPY_DIR}/py/mkrules.cmake)

# Suppress warnings in MicroPython core source files
set_source_files_properties(${MICROPY_SOURCE_PY} PROPERTIES
    COMPILE_FLAGS "-Wno-unused-variable -Wno-unused-function -Wno-sign-compare -Wno-old-style-definition -Wno-missing-field-initializers -Wno-maybe-uninitialized -Wno-shadow"
)
set_source_files_properties(${MICROPY_SOURCE_SHARED} PROPERTIES
    COMPILE_FLAGS "-Wno-unused-variable -Wno-unused-function -Wno-sign-compare -Wno-old-style-definition -Wno-missing-field-initializers -Wno-maybe-uninitialized"
)
