/*
 * Menu UI for Disobey Badge 2025 – FreeRTOS firmware.
 *
 * Renders a vertical list menu on the ST7789 320×170 display with submenu support.
 * Each item shows an icon and label.
 *
 * Layout (320 × 170 pixels, landscape):
 *
 *   ┌────────────────────────────────┐  y=0
 *   │  TITLE (scale 2, yellow)       │  y=4..35
 *   ├────────────────────────────────┤  y=38 (divider)
 *   │ 🎵 Item 0  (selected = cyan bg)│  y=42
 *   │   Item 1                       │  y=76
 *   │   Item 2                       │  y=110
 *   │   Item 3                       │  y=144
 *   └────────────────────────────────┘
 *
 * Up to 6 visible items with scrolling support.
 */

#include "menu_ui.h"
#include "st7789.h"
#include "badge_settings.h" /* New */
#include <string.h>
#include <stdio.h>

/* ── Layout constants ───────────────────────────────────────────────────── */
#define FONT_SCALE      1
#define CHAR_W          (8 * FONT_SCALE)
#define CHAR_H          (16 * FONT_SCALE)

#define TITLE_X         8
#define TITLE_Y         4
#define DIVIDER_Y       (TITLE_Y + CHAR_H + 4)
#define ITEMS_Y_START   (DIVIDER_Y + 4)
#define ITEM_ROW_H      (CHAR_H + 4)
#define ITEM_X          8
#define ICON_X          8
#define LABEL_X         24
#define VISIBLE_ITEMS   6

/* ── Module state ───────────────────────────────────────────────────────── */
static uint8_t s_last_selected = 0xFF;  /* sentinel "never drawn" */
static const menu_t *s_last_menu = NULL;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void draw_item(const menu_t *m, uint8_t idx, uint8_t view_row) {
    bool sel = (idx == m->selected);
    uint16_t bg = sel ? settings_get_accent_color() : MENU_COLOR_BG;
    uint16_t fg = sel ? MENU_COLOR_SEL_FG : MENU_COLOR_ITEM_FG;

    uint16_t y = ITEMS_Y_START + view_row * ITEM_ROW_H;

    /* Optimised: fill entire row background in one call (320 px wide) */
    st7789_fill_rect(0, y, ST7789_WIDTH, ITEM_ROW_H, bg);

    /* Draw icon and label */
    char buf[48];
    char icon = m->items[idx].icon;
    const char *label = m->items[idx].label;
    
    /* Format: "I Label" or "> Label" for selection */
    if (sel) {
        snprintf(buf, sizeof(buf), "> %s", label);
    } else if (icon != ' ' && icon != 0) {
        snprintf(buf, sizeof(buf), "%c %s", icon, label);
    } else {
        snprintf(buf, sizeof(buf), "  %s", label);
    }
    
    st7789_draw_string(ITEM_X, y + (ITEM_ROW_H - CHAR_H) / 2, buf, fg, bg, FONT_SCALE);
}

/* ── Public API ──────────────────────────────────────────────────────── */
void menu_init(menu_t *m, const char *title) {
    memset(m, 0, sizeof(*m));
    m->title     = title;
    m->num_items = 0;
    m->selected  = 0;
    m->parent    = NULL;
    s_last_selected = 0xFF;
    s_last_menu     = NULL;
}

bool menu_add_item(menu_t *m, char icon, const char *label, 
                   void (*action)(void), menu_t *submenu) {
    if (m->num_items >= MENU_MAX_ITEMS) return false;
    m->items[m->num_items].icon = icon;
    m->items[m->num_items].label = label;
    m->items[m->num_items].action = action;
    m->items[m->num_items].submenu = submenu;
    if (submenu) {
        submenu->parent = m;
    }
    m->num_items++;
    return true;
}

bool menu_back(menu_t **current_menu) {
    if (!current_menu || !*current_menu || !(*current_menu)->parent) {
        return false;
    }
    *current_menu = (*current_menu)->parent;
    return true;
}

bool menu_enter_submenu(menu_t **current_menu) {
    if (!current_menu || !*current_menu) {
        return false;
    }
    menu_t *m = *current_menu;
    if (m->selected >= m->num_items) {
        return false;
    }
    menu_item_t *item = &m->items[m->selected];
    if (!item->submenu) {
        return false;
    }
    item->submenu->selected = 0;
    *current_menu = item->submenu;
    return true;
}

void menu_navigate_up(menu_t *m) {
    if (m->num_items == 0) return;
    m->selected = (m->selected == 0) ? m->num_items - 1 : m->selected - 1;
}

void menu_navigate_down(menu_t *m) {
    if (m->num_items == 0) return;
    m->selected = (m->selected + 1) % m->num_items;
}

void menu_select(menu_t *m) {
    if (m->num_items == 0) return;
    if (m->items[m->selected].action) {
        m->items[m->selected].action();
    }
}

void menu_draw(menu_t *m, bool force) {
    bool full_redraw = force || (s_last_menu != m);

    if (full_redraw) {
        /* Clear entire screen */
        st7789_fill(MENU_COLOR_BG);

        /* Title */
        st7789_draw_string(TITLE_X, TITLE_Y, m->title,
                           MENU_COLOR_TITLE_FG, MENU_COLOR_BG, FONT_SCALE);

        /* Divider line */
        st7789_fill_rect(0, DIVIDER_Y, ST7789_WIDTH, 2, settings_get_accent_color());

        s_last_menu = m;
        s_last_selected = 0xFF;  /* force item redraw */
    }

    if (!full_redraw && s_last_selected == m->selected) return;  /* nothing changed */

    /*
     * Viewport: show VISIBLE_ITEMS rows, scrolled so that the selected item
     * is always visible.
     */
    uint8_t view_top = 0;
    if (m->num_items > VISIBLE_ITEMS) {
        if (m->selected >= VISIBLE_ITEMS) {
            view_top = m->selected - VISIBLE_ITEMS + 1;
        }
    }

    for (uint8_t vi = 0; vi < VISIBLE_ITEMS; vi++) {
        uint8_t idx = view_top + vi;
        if (idx >= m->num_items) {
            /* Clear empty row */
            uint16_t y = ITEMS_Y_START + vi * ITEM_ROW_H;
            st7789_fill_rect(0, y, ST7789_WIDTH, ITEM_ROW_H, MENU_COLOR_BG);
        } else {
            draw_item(m, idx, vi);
        }
    }

    s_last_selected = m->selected;
}
