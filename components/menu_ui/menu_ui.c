/*
 * Menu UI for Disobey Badge 2025/26 – FreeRTOS firmware.
 *
 * Two rendering modes:
 *   1) GRID mode (grid_mode == true) – 2×3 icon grid for the root/main menu.
 *      Each cell shows a 24×24 bitmap at 2× scale (48×48) with a label below.
 *   2) LIST mode (default) – vertical scrollable list with text icons.
 *
 * Display: 320 × 170 pixels, landscape.
 */

#include "menu_ui.h"
#include "menu_icons.h"
#include "st7789.h"
#include "badge_settings.h"
#include <string.h>
#include <stdio.h>

/* ── Shared layout constants ────────────────────────────────────────────── */
#define FONT_SCALE      1
#define CHAR_W          (8 * FONT_SCALE)
#define CHAR_H          (16 * FONT_SCALE)

#define TITLE_X         8
#define TITLE_Y         4
#define DIVIDER_Y       (TITLE_Y + CHAR_H + 4)

/* ── List-mode constants ────────────────────────────────────────────────── */
#define ITEMS_Y_START   (DIVIDER_Y + 4)
#define ITEM_ROW_H      (CHAR_H + 4)
#define ITEM_X          8
#define VISIBLE_ITEMS   6

/* ── Grid-mode constants ────────────────────────────────────────────────── */
#define GRID_COLS       3
#define GRID_ROWS       2
#define GRID_MAX_ITEMS  (GRID_COLS * GRID_ROWS)   /* 6 */

#define ICON_SCALE      2
#define ICON_PX         (MENU_ICON_W * ICON_SCALE) /* 48 */

/*
 * Grid area starts just below the divider line.
 * Available height = 170 - DIVIDER_Y - 2 (divider) - 2 (padding) ≈ 142 px
 * Two rows → each cell ~71 px tall.
 * Cell width  = 320 / 3 ≈ 106 px.
 */
#define GRID_Y_START    (DIVIDER_Y + 4)
#define GRID_CELL_W     (ST7789_WIDTH / GRID_COLS)                        /* 106 */
#define GRID_CELL_H     ((ST7789_HEIGHT - GRID_Y_START) / GRID_ROWS)      /* ~73 */

/* Selection border thickness */
#define SEL_BORDER      2

/* ── Module state ───────────────────────────────────────────────────────── */
static uint8_t s_last_selected = 0xFF;  /* sentinel "never drawn" */
static const menu_t *s_last_menu = NULL;

/* ══════════════════════════════════════════════════════════════════════════
 *  LIST-MODE helpers
 * ══════════════════════════════════════════════════════════════════════════ */
static void draw_list_item(const menu_t *m, uint8_t idx, uint8_t view_row) {
    bool sel = (idx == m->selected);
    uint16_t bg = sel ? settings_get_accent_color() : MENU_COLOR_BG;
    uint16_t fg = sel ? MENU_COLOR_SEL_FG : MENU_COLOR_ITEM_FG;

    uint16_t y = ITEMS_Y_START + view_row * ITEM_ROW_H;

    st7789_fill_rect(0, y, ST7789_WIDTH, ITEM_ROW_H, bg);

    char buf[48];
    char icon = m->items[idx].icon;
    const char *label = m->items[idx].label;

    if (sel) {
        snprintf(buf, sizeof(buf), "> %s", label);
    } else if (icon != ' ' && icon != 0) {
        snprintf(buf, sizeof(buf), "%c %s", icon, label);
    } else {
        snprintf(buf, sizeof(buf), "  %s", label);
    }

    st7789_draw_string(ITEM_X, y + (ITEM_ROW_H - CHAR_H) / 2, buf, fg, bg, FONT_SCALE);
}

static void draw_list(menu_t *m, bool full_redraw) {
    if (!full_redraw && s_last_selected == m->selected) return;

    uint8_t view_top = 0;
    if (m->num_items > VISIBLE_ITEMS) {
        if (m->selected >= VISIBLE_ITEMS) {
            view_top = m->selected - VISIBLE_ITEMS + 1;
        }
    }

    for (uint8_t vi = 0; vi < VISIBLE_ITEMS; vi++) {
        uint8_t idx = view_top + vi;
        if (idx >= m->num_items) {
            uint16_t y = ITEMS_Y_START + vi * ITEM_ROW_H;
            st7789_fill_rect(0, y, ST7789_WIDTH, ITEM_ROW_H, MENU_COLOR_BG);
        } else {
            draw_list_item(m, idx, vi);
        }
    }

    s_last_selected = m->selected;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  GRID-MODE helpers
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * Draw a single grid cell.
 *
 *  ┌─────────────────┐
 *  │   (border if sel)│
 *  │   ██ icon ██     │
 *  │                  │
 *  │   Label text     │
 *  └─────────────────┘
 */
static void draw_grid_cell(const menu_t *m, uint8_t idx) {
    uint8_t col = idx % GRID_COLS;
    uint8_t row = idx / GRID_COLS;

    uint16_t cx = col * GRID_CELL_W;
    uint16_t cy = GRID_Y_START + row * GRID_CELL_H;

    bool sel = (idx == m->selected);
    uint16_t accent = settings_get_accent_color();

    /* 1. Clear cell background */
    st7789_fill_rect(cx, cy, GRID_CELL_W, GRID_CELL_H, MENU_COLOR_BG);

    /* 2. Selection border (rounded-rect feel with 4 filled rects) */
    if (sel) {
        /* top */
        st7789_fill_rect(cx + 1, cy + 1, GRID_CELL_W - 2, SEL_BORDER, accent);
        /* bottom */
        st7789_fill_rect(cx + 1, cy + GRID_CELL_H - SEL_BORDER - 1,
                         GRID_CELL_W - 2, SEL_BORDER, accent);
        /* left */
        st7789_fill_rect(cx + 1, cy + 1, SEL_BORDER, GRID_CELL_H - 2, accent);
        /* right */
        st7789_fill_rect(cx + GRID_CELL_W - SEL_BORDER - 1, cy + 1,
                         SEL_BORDER, GRID_CELL_H - 2, accent);
    }

    /* 3. Icon — centred horizontally within the cell, offset a bit from top */
    uint16_t icon_x = cx + (GRID_CELL_W - ICON_PX) / 2;
    uint16_t icon_y = cy + 6;

    const uint8_t *bmp = m->items[idx].bitmap_icon;
    if (bmp) {
        uint16_t icon_fg = sel ? accent : MENU_COLOR_ITEM_FG;
        st7789_draw_bitmap(icon_x, icon_y, bmp,
                           MENU_ICON_W, MENU_ICON_H,
                           icon_fg, MENU_COLOR_BG, ICON_SCALE);
    }

    /* 4. Label — centred below the icon */
    const char *label = m->items[idx].label;
    uint8_t label_len = (uint8_t)strlen(label);
    uint16_t label_px = label_len * CHAR_W;
    uint16_t lx = cx + (GRID_CELL_W - label_px) / 2;
    uint16_t ly = icon_y + ICON_PX + 4;

    uint16_t fg = sel ? accent : MENU_COLOR_ITEM_FG;
    st7789_draw_string(lx, ly, label, fg, MENU_COLOR_BG, FONT_SCALE);
}

static void draw_grid(menu_t *m, bool full_redraw) {
    if (!full_redraw && s_last_selected == m->selected) return;

    if (full_redraw) {
        /* Redraw every cell */
        uint8_t n = m->num_items;
        if (n > GRID_MAX_ITEMS) n = GRID_MAX_ITEMS;
        for (uint8_t i = 0; i < n; i++) {
            draw_grid_cell(m, i);
        }
    } else {
        /* Incremental: redraw only old-selected and new-selected cells */
        if (s_last_selected < m->num_items) {
            draw_grid_cell(m, s_last_selected);
        }
        draw_grid_cell(m, m->selected);
    }

    s_last_selected = m->selected;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════════════ */

void menu_init(menu_t *m, const char *title) {
    memset(m, 0, sizeof(*m));
    m->title     = title;
    m->num_items = 0;
    m->selected  = 0;
    m->parent    = NULL;
    m->grid_mode = false;
    s_last_selected = 0xFF;
    s_last_menu     = NULL;
}

bool menu_add_item(menu_t *m, char icon, const uint8_t *bitmap_icon,
                   const char *label,
                   void (*action)(void), menu_t *submenu) {
    if (m->num_items >= MENU_MAX_ITEMS) return false;
    m->items[m->num_items].icon        = icon;
    m->items[m->num_items].bitmap_icon = bitmap_icon;
    m->items[m->num_items].label       = label;
    m->items[m->num_items].action      = action;
    m->items[m->num_items].submenu     = submenu;
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

/* ── Navigation ─────────────────────────────────────────────────────────── */

void menu_navigate_up(menu_t *m) {
    if (m->num_items == 0) return;
    if (m->grid_mode) {
        /* Move up one row; wrap to bottom of same column */
        if (m->selected >= GRID_COLS) {
            m->selected -= GRID_COLS;
        } else {
            /* Wrap: go to same column in last row */
            uint8_t col = m->selected % GRID_COLS;
            uint8_t last_row = (m->num_items - 1) / GRID_COLS;
            uint8_t target = last_row * GRID_COLS + col;
            if (target >= m->num_items) target -= GRID_COLS;
            m->selected = target;
        }
    } else {
        m->selected = (m->selected == 0) ? m->num_items - 1 : m->selected - 1;
    }
}

void menu_navigate_down(menu_t *m) {
    if (m->num_items == 0) return;
    if (m->grid_mode) {
        uint8_t next = m->selected + GRID_COLS;
        if (next < m->num_items) {
            m->selected = next;
        } else {
            /* Wrap to top of same column */
            m->selected = m->selected % GRID_COLS;
        }
    } else {
        m->selected = (m->selected + 1) % m->num_items;
    }
}

void menu_navigate_left(menu_t *m) {
    if (!m->grid_mode || m->num_items == 0) return;
    uint8_t col = m->selected % GRID_COLS;
    if (col == 0) {
        /* Wrap to last column in same row (or last item if row is partial) */
        uint8_t row_start = m->selected;
        uint8_t target = row_start + GRID_COLS - 1;
        if (target >= m->num_items) target = m->num_items - 1;
        m->selected = target;
    } else {
        m->selected--;
    }
}

void menu_navigate_right(menu_t *m) {
    if (!m->grid_mode || m->num_items == 0) return;
    uint8_t col = m->selected % GRID_COLS;
    uint8_t row_start = m->selected - col;
    if (col == GRID_COLS - 1 || m->selected + 1 >= m->num_items) {
        /* Wrap to first column in same row */
        m->selected = row_start;
    } else {
        m->selected++;
    }
}

/* ── Selection ──────────────────────────────────────────────────────────── */

void menu_select(menu_t *m) {
    if (m->num_items == 0) return;
    if (m->items[m->selected].action) {
        m->items[m->selected].action();
    }
}

/* ── Drawing ────────────────────────────────────────────────────────────── */

void menu_draw(menu_t *m, bool force) {
    bool full_redraw = force || (s_last_menu != m);

    if (full_redraw) {
        st7789_fill(MENU_COLOR_BG);

        /* Title */
        st7789_draw_string(TITLE_X, TITLE_Y, m->title,
                           MENU_COLOR_TITLE_FG, MENU_COLOR_BG, FONT_SCALE);

        /* Divider */
        st7789_fill_rect(0, DIVIDER_Y, ST7789_WIDTH, 2, settings_get_accent_color());

        s_last_menu = m;
        s_last_selected = 0xFF;
    }

    if (m->grid_mode) {
        draw_grid(m, full_redraw || s_last_selected == 0xFF);
    } else {
        draw_list(m, full_redraw || s_last_selected == 0xFF);
    }
}
