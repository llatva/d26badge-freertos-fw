#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "badge_settings.h"

/* Maximum items per menu */
#define MENU_MAX_ITEMS 12

/* ── Colour theme ────────────────────────────────────────────────────────── */
#define MENU_COLOR_BG          0x0000u   /* black  */
#define MENU_COLOR_SEL_FG      0x0000u   /* black  */
#define MENU_COLOR_DIVIDER     0x8410u   /* gray   */
#define MENU_COLOR_SEL_BG      settings_get_accent_color()
#define MENU_COLOR_ITEM_FG     settings_get_text_color()
#define MENU_COLOR_TITLE_FG    settings_get_accent_color()

/* ── Menu item icon (simple character) ───────────────────────────────────── */
typedef struct {
    char icon;              /* Single character icon (e.g., '♪', '⚙', '📁') */
    const char *label;      /* Item label text */
    void (*action)(void);   /* Called when item is selected */
    struct menu_t *submenu; /* Pointer to submenu (NULL if no submenu) */
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
};

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the menu context with a title and no items.
 */
void menu_init(menu_t *m, const char *title);

/**
 * @brief  Append an item to the menu (with icon and optional submenu).
 *         Returns false if already at maximum.
 */
bool menu_add_item(menu_t *m, char icon, const char *label, 
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
 */
void menu_navigate_up(menu_t *m);

/**
 * @brief  Move selection down (wraps around).
 */
void menu_navigate_down(menu_t *m);

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
