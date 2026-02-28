#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "badge_settings.h"

/* Maximum items per menu */
#define MENU_MAX_ITEMS 16

/* ── Colour theme ────────────────────────────────────────────────────────── */
#define MENU_COLOR_BG          0x0000u   /* black  */
#define MENU_COLOR_SEL_FG      0x0000u   /* black  */
#define MENU_COLOR_DIVIDER     0x8410u   /* gray   */
#define MENU_COLOR_SEL_BG      settings_get_accent_color()
#define MENU_COLOR_ITEM_FG     settings_get_text_color()
#define MENU_COLOR_TITLE_FG    settings_get_accent_color()

/* ── Menu item ───────────────────────────────────────────────────────────── */
typedef struct {
    char icon;                  /* Single character icon (used in list mode) */
    const uint8_t *bitmap_icon; /* 24×24 monochrome bitmap (NULL = none) */
    const char *label;          /* Item label text */
    void (*action)(void);       /* Called when item is selected */
    struct menu_t *submenu;     /* Pointer to submenu (NULL if no submenu) */
} menu_item_t;

/* Forward declaration for recursive structure */
typedef struct menu_t menu_t;

/* ── Menu context ────────────────────────────────────────────────────────── */
struct menu_t {
    const char    *title;
    menu_item_t    items[MENU_MAX_ITEMS];
    uint8_t        num_items;
    uint8_t        selected;   /* currently highlighted item */
    menu_t        *parent;     /* parent menu for back navigation */
    bool           grid_mode;  /* true → render as icon grid (2×3) */
};

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the menu context with a title and no items.
 */
void menu_init(menu_t *m, const char *title);

/**
 * @brief  Append an item to the menu (with icon and optional submenu).
 *         Returns false if already at maximum.
 * @param bitmap_icon  Pointer to a 24×24 monochrome bitmap (NULL for none).
 */
bool menu_add_item(menu_t *m, char icon, const uint8_t *bitmap_icon,
                   const char *label,
                   void (*action)(void), menu_t *submenu);

/**
 * @brief  Navigate to parent menu (for submenu back navigation).
 *         Returns true if parent exists, false if already at root.
 */
bool menu_back(menu_t **current_menu);

/**
 * @brief  Navigate to submenu (if current item has one).
 *         Returns true if submenu exists, false if current item is leaf.
 */
bool menu_enter_submenu(menu_t **current_menu);

/**
 * @brief  Move selection up (wraps around).
 *         In grid mode: moves up one row.
 */
void menu_navigate_up(menu_t *m);

/**
 * @brief  Move selection down (wraps around).
 *         In grid mode: moves down one row.
 */
void menu_navigate_down(menu_t *m);

/**
 * @brief  Move selection left in grid mode (wraps within row).
 *         In list mode this is a no-op.
 */
void menu_navigate_left(menu_t *m);

/**
 * @brief  Move selection right in grid mode (wraps within row).
 *         In list mode this is a no-op.
 */
void menu_navigate_right(menu_t *m);

/**
 * @brief  Activate the currently selected item (call its action or enter submenu).
 */
void menu_select(menu_t *m);

/**
 * @brief  Redraw the full menu to the ST7789 display.
 *         Only redraws when @p force is true or the selection has changed
 *         since the last draw.
 */
void menu_draw(menu_t *m, bool force);
