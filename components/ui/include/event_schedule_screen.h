/*
 * Event schedule screen – displays event schedule fetched from a JSON URL.
 *
 * Shows one day at a time (Friday / Saturday).  LEFT/RIGHT navigates
 * between days; UP/DOWN scrolls within a day.  B exits.
 *
 * If the URL is unreachable (no WiFi, server down, etc.) the screen
 * falls back to a day template with "Unable to fetch data".
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── Capacity limits ────────────────────────────────────────────────────── */
#define EVT_MAX_DAYS          2
#define EVT_MAX_EVENTS       16
#define EVT_TIME_LEN         12     /* e.g. "10:00"  */
#define EVT_TITLE_LEN        40     /* max chars per title */
#define EVT_DAY_NAME_LEN     16     /* e.g. "Friday" */

/* ── Data structures ────────────────────────────────────────────────────── */

typedef struct {
    char time[EVT_TIME_LEN];
    char title[EVT_TITLE_LEN];
} event_entry_t;

typedef struct {
    char          name[EVT_DAY_NAME_LEN];
    event_entry_t events[EVT_MAX_EVENTS];
    int           num_events;
} event_day_t;

typedef struct {
    event_day_t days[EVT_MAX_DAYS];
    int         num_days;
    int         current_day;       /* Index into days[] */
    int         scroll_offset;     /* Scroll within current day */
    bool        fetch_ok;          /* true if JSON was fetched successfully */
} event_schedule_screen_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the screen and attempt to fetch the schedule JSON.
 *         On failure the screen is populated with an empty template.
 */
void event_schedule_screen_init(event_schedule_screen_t *scr);

/**
 * @brief  Render the current day's schedule to the display.
 */
void event_schedule_screen_draw(event_schedule_screen_t *scr);

/**
 * @brief  Switch to the next day (wraps around).
 */
void event_schedule_screen_next_day(event_schedule_screen_t *scr);

/**
 * @brief  Switch to the previous day (wraps around).
 */
void event_schedule_screen_prev_day(event_schedule_screen_t *scr);

/**
 * @brief  Scroll up within the current day's event list.
 */
void event_schedule_screen_scroll_up(event_schedule_screen_t *scr);

/**
 * @brief  Scroll down within the current day's event list.
 */
void event_schedule_screen_scroll_down(event_schedule_screen_t *scr);
