/*
 * SAO EEPROM screen – reads and displays I2C EEPROM at address 0x50
 *
 * Displays raw EEPROM contents in hex + ASCII format (classic hex-dump).
 * Supports UP/DOWN scrolling.  B exits.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* How many bytes to read from the EEPROM */
#define SAO_EEPROM_READ_SIZE  256

typedef struct {
    uint8_t  data[SAO_EEPROM_READ_SIZE]; /* Raw EEPROM contents            */
    int      bytes_read;                 /* Actual bytes successfully read  */
    bool     read_ok;                    /* true if I2C read succeeded      */
    int      scroll_offset;              /* Current scroll line offset      */
    char     error_msg[64];              /* Human-readable error string     */
} sao_eeprom_screen_t;

/**
 * @brief  Initialise the SAO EEPROM screen and read the EEPROM.
 *         Sets up I2C master, reads 256 bytes from address 0x50,
 *         then tears down the I2C bus.
 */
void sao_eeprom_screen_init(sao_eeprom_screen_t *scr);

/**
 * @brief  Render the hex + ASCII dump to the display.
 */
void sao_eeprom_screen_draw(sao_eeprom_screen_t *scr);

/**
 * @brief  Scroll up by one line.
 */
void sao_eeprom_screen_scroll_up(sao_eeprom_screen_t *scr);

/**
 * @brief  Scroll down by one line.
 */
void sao_eeprom_screen_scroll_down(sao_eeprom_screen_t *scr);
