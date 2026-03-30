/*
 * Event schedule screen – fetches a JSON schedule from a configured URL
 * and displays it day-by-day on the ST7789 display.
 *
 * JSON format expected from the server:
 *
 *   {
 *     "days": [
 *       {
 *         "name": "Friday",
 *         "events": [
 *           { "time": "10:00", "title": "Opening Ceremony" },
 *           ...
 *         ]
 *       },
 *       { "name": "Saturday", "events": [ ... ] }
 *     ]
 *   }
 *
 * If the fetch fails the screen shows a two-day template
 * (Friday / Saturday) with the message "Unable to fetch data".
 */

#include "event_schedule_screen.h"
#include "st7789.h"
#include "badge_settings.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

#define TAG "evt_schedule"

/* ── Firmware configuration: schedule URL ────────────────────────────────── */
#ifndef EVENT_SCHEDULE_URL
#define EVENT_SCHEDULE_URL "http://badge.disobey.fi/api/schedule"
#endif

/* ── Display layout ──────────────────────────────────────────────────────── */
#define COLOR_BG         0x0000  /* Black */
#define TITLE_H          22      /* Title bar height in pixels */
#define LINE_H           14      /* Line height for event rows (scale 1) */
#define NAV_H            16      /* Navigation bar height */
#define VISIBLE_LINES    8       /* Max event rows visible at once */
#define HTTP_BUF_SIZE    4096    /* Max HTTP response size */

/* ── Fallback template (used when fetch fails) ──────────────────────────── */

static void populate_fallback(event_schedule_screen_t *scr)
{
    scr->num_days = EVT_MAX_DAYS;

    /* Day 0: Friday */
    strncpy(scr->days[0].name, "Friday", EVT_DAY_NAME_LEN - 1);
    scr->days[0].num_events = 0;

    /* Day 1: Saturday */
    strncpy(scr->days[1].name, "Saturday", EVT_DAY_NAME_LEN - 1);
    scr->days[1].num_events = 0;
}

/* ── JSON parsing ────────────────────────────────────────────────────────── */

static bool parse_schedule_json(event_schedule_screen_t *scr, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return false;
    }

    cJSON *days_arr = cJSON_GetObjectItem(root, "days");
    if (!cJSON_IsArray(days_arr)) {
        ESP_LOGE(TAG, "Missing 'days' array");
        cJSON_Delete(root);
        return false;
    }

    int day_count = cJSON_GetArraySize(days_arr);
    if (day_count > EVT_MAX_DAYS) day_count = EVT_MAX_DAYS;
    scr->num_days = day_count;

    for (int d = 0; d < day_count; d++) {
        cJSON *day_obj = cJSON_GetArrayItem(days_arr, d);
        event_day_t *day = &scr->days[d];
        memset(day, 0, sizeof(*day));

        /* Day name */
        cJSON *name = cJSON_GetObjectItem(day_obj, "name");
        if (cJSON_IsString(name) && name->valuestring) {
            strncpy(day->name, name->valuestring, EVT_DAY_NAME_LEN - 1);
        }

        /* Events array */
        cJSON *events_arr = cJSON_GetObjectItem(day_obj, "events");
        if (!cJSON_IsArray(events_arr)) continue;

        int evt_count = cJSON_GetArraySize(events_arr);
        if (evt_count > EVT_MAX_EVENTS) evt_count = EVT_MAX_EVENTS;
        day->num_events = evt_count;

        for (int e = 0; e < evt_count; e++) {
            cJSON *evt = cJSON_GetArrayItem(events_arr, e);
            event_entry_t *entry = &day->events[e];

            cJSON *t = cJSON_GetObjectItem(evt, "time");
            if (cJSON_IsString(t) && t->valuestring)
                strncpy(entry->time, t->valuestring, EVT_TIME_LEN - 1);

            cJSON *ti = cJSON_GetObjectItem(evt, "title");
            if (cJSON_IsString(ti) && ti->valuestring)
                strncpy(entry->title, ti->valuestring, EVT_TITLE_LEN - 1);
        }
    }

    cJSON_Delete(root);
    return scr->num_days > 0;
}

/* ── HTTP fetch ──────────────────────────────────────────────────────────── */

static bool fetch_schedule(event_schedule_screen_t *scr)
{
    char *buf = malloc(HTTP_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
        return false;
    }

    esp_http_client_config_t config = {
        .url = EVENT_SCHEDULE_URL,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        free(buf);
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(buf);
        return false;
    }

    int content_len = esp_http_client_fetch_headers(client);
    (void)content_len;  /* May be -1 for chunked transfers */

    int total_read = 0;
    int read_len;
    while (total_read < HTTP_BUF_SIZE - 1) {
        read_len = esp_http_client_read(client, buf + total_read,
                                        HTTP_BUF_SIZE - 1 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "HTTP %d – received %d bytes", status, total_read);

    bool ok = false;
    if (status == 200 && total_read > 0) {
        ok = parse_schedule_json(scr, buf);
    }

    free(buf);
    return ok;
}

/* ── Init ────────────────────────────────────────────────────────────────── */

void event_schedule_screen_init(event_schedule_screen_t *scr)
{
    if (!scr) return;
    memset(scr, 0, sizeof(*scr));

    /* Try to fetch live data */
    scr->fetch_ok = fetch_schedule(scr);

    if (!scr->fetch_ok) {
        ESP_LOGW(TAG, "Using fallback schedule template");
        populate_fallback(scr);
    }
}

/* ── Draw ────────────────────────────────────────────────────────────────── */

void event_schedule_screen_draw(event_schedule_screen_t *scr)
{
    if (!scr) return;

    uint16_t ACCENT = settings_get_accent_color();
    uint16_t TEXT   = settings_get_text_color();

    st7789_fill(COLOR_BG);

    /* ── Title: "Schedule" ────────────────────────────────────────────── */
    st7789_draw_string(4, 2, "Schedule", ACCENT, COLOR_BG, 2);

    /* Day indicator (right-aligned in title area) */
    if (scr->num_days > 0) {
        char day_ind[8];
        snprintf(day_ind, sizeof(day_ind), "%d/%d", scr->current_day + 1, scr->num_days);
        st7789_draw_string(280, 6, day_ind, TEXT, COLOR_BG, 1);
    }

    /* Divider */
    st7789_fill_rect(0, TITLE_H, 320, 1, ACCENT);

    /* ── Day name ─────────────────────────────────────────────────────── */
    int y = TITLE_H + 4;
    event_day_t *day = &scr->days[scr->current_day];
    st7789_draw_string(4, y, day->name, ACCENT, COLOR_BG, 1);
    y += LINE_H + 2;

    /* ── Event list or "Unable to fetch data" ─────────────────────────── */
    if (!scr->fetch_ok) {
        st7789_draw_string(4, y + 20, "Unable to fetch data", TEXT, COLOR_BG, 1);
    } else if (day->num_events == 0) {
        st7789_draw_string(4, y + 20, "No events scheduled", TEXT, COLOR_BG, 1);
    } else {
        for (int i = 0; i < VISIBLE_LINES; i++) {
            int idx = scr->scroll_offset + i;
            if (idx >= day->num_events) break;

            event_entry_t *evt = &day->events[idx];
            int row_y = y + i * LINE_H;

            /* Time column */
            st7789_draw_string(4, row_y, evt->time, ACCENT, COLOR_BG, 1);

            /* Title column */
            st7789_draw_string(60, row_y, evt->title, TEXT, COLOR_BG, 1);
        }

        /* Scroll indicator when content overflows */
        if (day->num_events > VISIBLE_LINES) {
            int bar_total_h = VISIBLE_LINES * LINE_H;
            int bar_h = bar_total_h * VISIBLE_LINES / day->num_events;
            if (bar_h < 6) bar_h = 6;
            int bar_y = y;
            if (day->num_events > VISIBLE_LINES) {
                bar_y = y + (bar_total_h - bar_h) * scr->scroll_offset /
                        (day->num_events - VISIBLE_LINES);
            }
            st7789_fill_rect(316, bar_y, 3, bar_h, ACCENT);
        }
    }

    /* ── Navigation bar ───────────────────────────────────────────────── */
    int nav_y = 170 - NAV_H;
    st7789_fill_rect(0, nav_y - 1, 320, 1, ACCENT);
    st7789_draw_string(4, nav_y + 2, "B:back", ACCENT, COLOR_BG, 1);
    st7789_draw_string(110, nav_y + 2, "L/R:day", 0x7BEF, COLOR_BG, 1);
    if (day->num_events > VISIBLE_LINES) {
        st7789_draw_string(220, nav_y + 2, "U/D:scroll", 0x7BEF, COLOR_BG, 1);
    }
}

/* ── Day navigation ──────────────────────────────────────────────────────── */

void event_schedule_screen_next_day(event_schedule_screen_t *scr)
{
    if (!scr || scr->num_days <= 1) return;
    scr->current_day = (scr->current_day + 1) % scr->num_days;
    scr->scroll_offset = 0;
}

void event_schedule_screen_prev_day(event_schedule_screen_t *scr)
{
    if (!scr || scr->num_days <= 1) return;
    scr->current_day = (scr->current_day - 1 + scr->num_days) % scr->num_days;
    scr->scroll_offset = 0;
}

/* ── Scroll ──────────────────────────────────────────────────────────────── */

void event_schedule_screen_scroll_up(event_schedule_screen_t *scr)
{
    if (!scr) return;
    if (scr->scroll_offset > 0) scr->scroll_offset--;
}

void event_schedule_screen_scroll_down(event_schedule_screen_t *scr)
{
    if (!scr) return;
    event_day_t *day = &scr->days[scr->current_day];
    int max_offset = day->num_events - VISIBLE_LINES;
    if (max_offset < 0) max_offset = 0;
    if (scr->scroll_offset < max_offset) scr->scroll_offset++;
}
