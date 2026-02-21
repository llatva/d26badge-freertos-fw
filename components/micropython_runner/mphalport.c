/*
 * Minimal HAL port implementation
 */

#include "py/mphal.h"
#include "py/mpprint.h"
#include <stdio.h>

// Print structure for MicroPython
static void mp_hal_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    printf("%.*s", (int)len, str);
}

const mp_print_t mp_plat_print = {NULL, mp_hal_print_strn};
