/*
 * RaceCondition – Outrun-style pseudo-3D racing game
 *
 * The player drives through a simplified version of a racing circuit.
 * The road is rendered with a
 * classic perspective projection, C64-inspired colour palette and
 * chunky pixel style.  Avoid traffic, survive the laps and rack up
 * distance!
 *
 * Double-buffered rendering: every frame is composed entirely in a
 * RAM buffer and then blitted to the display in one SPI transaction,
 * so the user never sees a partially-drawn frame.
 *
 * Controls:
 *   LEFT / RIGHT  – steer
 *   A             – accelerate
 *   B             – exit (handled in main.c)
 */

#include "race_condition.h"
#include "st7789.h"
#include "font8x16.h"
#include "sk6812.h"
#include "buttons.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_random.h"
#include "esp_log.h"

static const char *TAG = "race";

/* ── Display ─────────────────────────────────────────────────────────── */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170

/* ── C64-inspired colour palette (RGB565) ────────────────────────────── */
#define C64_BLACK       0x0000u
#define C64_WHITE       0xFFFFu
#define C64_RED         0xA800u
#define C64_CYAN        0x57FFu
#define C64_PURPLE      0x8817u
#define C64_GREEN       0x0580u
#define C64_BLUE        0x0017u
#define C64_YELLOW      0xEF40u
#define C64_ORANGE      0xCC40u
#define C64_BROWN       0x6200u
#define C64_LIGHT_RED   0xF98Cu
#define C64_DARK_GREY   0x4A49u
#define C64_GREY        0x7BCFu
#define C64_LIGHT_GREEN 0xA7E4u
#define C64_LIGHT_BLUE  0x653Fu
#define C64_LIGHT_GREY  0x9CD3u

/* ── Road / rendering constants ──────────────────────────────────────── */
#define ROAD_LINES        100          /* scanlines that form the road   */
#define HORIZON_Y         50           /* horizon line (pixels from top) */

#define ROAD_BASE_W       180          /* road width at screen bottom    */
#define ROAD_TOP_W        24           /* road width at horizon          */

#define RUMBLE_STRIPE_LEN 12           /* scanlines per rumble-strip pair */
#define CENTRE_DASH_LEN   8

/* ── Game-play constants ─────────────────────────────────────────────── */
#define MAX_SPEED         200
#define ACCEL             3
#define DECEL             2
#define STEER_SPEED       5
#define ROAD_MAX_OFFSET   140

#define MAX_OTHER_CARS    4
#define CAR_SPAWN_DIST    600

/* ── Race track – sequence of curve segments ────────────────────────── */
/* Each segment: { curve_strength (negative=left), length_in_ticks }     */
/* A challenging 5.8 km layout with long straights, tight chicanes       */
/* and sweeping curves.                                                  */

typedef struct {
    int8_t  curve;     /* –5 .. +5  (0 = straight) */
    int16_t length;    /* ticks (higher = longer segment) */
} track_seg_t;

static const track_seg_t RACE_CONDITION_TRACK[] = {
    {  0, 300 },   /* Start / finish straight          */
    {  3, 100 },   /* First chicane right               */
    { -3,  80 },   /* First chicane left                */
    {  0, 180 },   /* Long curve approach               */
    {  2, 140 },   /* Sweeping right                    */
    {  0, 100 },   /* Short straight                    */
    { -3, 100 },   /* Second chicane left               */
    {  3,  80 },   /* Second chicane right              */
    {  0,  80 },   /* Straight run                      */
    {  2, 120 },   /* Double right turn 1               */
    {  2, 100 },   /* Double right turn 2               */
    {  0, 200 },   /* Long back straight                */
    { -4, 100 },   /* Esses left                        */
    {  4,  80 },   /* Esses right                       */
    { -3,  80 },   /* Esses exit left                   */
    {  0, 250 },   /* Final straight                    */
    {  3, 200 },   /* Long sweeping right               */
    {  0, 120 },   /* Pit straight back to start        */
};
#define NUM_TRACK_SEGS  (sizeof(RACE_CONDITION_TRACK) / sizeof(RACE_CONDITION_TRACK[0]))

/* ── Other car / traffic ─────────────────────────────────────────────── */
typedef struct {
    int16_t  lane;       /* lateral offset from road centre   */
    int32_t  dist;       /* world distance ahead of player    */
    uint16_t color;      /* body colour                       */
    bool     active;
} other_car_t;

/* ── Game state ──────────────────────────────────────────────────────── */
typedef struct {
    /* Player */
    int16_t  speed;          /* 0 .. MAX_SPEED                  */
    int16_t  steer_pos;      /* lateral position –MAX..+MAX     */
    uint32_t distance;       /* total distance travelled (score) */
    uint8_t  lap;            /* current lap number               */
    uint32_t lap_distance;   /* distance within this lap         */
    uint32_t lap_length;     /* total length of one lap          */

    /* Track */
    uint8_t  seg_index;      /* current segment in RACE_CONDITION_TRACK  */
    int16_t  seg_remain;     /* ticks remaining in segment      */
    int8_t   cur_curve;      /* curve of current segment        */

    /* Visual */
    uint32_t frame;          /* frame counter for animation     */
    int16_t  road_offset;    /* cumulative curve visual offset  */

    /* Traffic */
    other_car_t cars[MAX_OTHER_CARS];
    uint32_t next_car_dist;

    /* State */
    bool     game_over;
} game_state_t;

static game_state_t g_game;

/* ── Double-buffer frame buffer ─────────────────────────────────────── */
/* 320 × 170 × 2 bytes ≈ 106 KB – allocated once on first init.        */
/* Pixels are stored in big-endian (wire) byte order so that the buffer */
/* can be blitted directly via SPI without per-pixel conversion.        */

#define FB_PIXELS  (SCREEN_WIDTH * SCREEN_HEIGHT)
static uint16_t *fb;

/* Byte-swap a host-order RGB565 value to wire (big-endian) order. */
#define SWAP16(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
    uint16_t cs = SWAP16(color);
    /* Clip to screen */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH  - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    /* Fill first row, then memcpy to the rest */
    uint16_t *row = &fb[y * SCREEN_WIDTH + x];
    for (int i = 0; i < w; i++) row[i] = cs;
    for (int r = 1; r < h; r++) {
        memcpy(&fb[(y + r) * SCREEN_WIDTH + x], row, (size_t)w * sizeof(uint16_t));
    }
}

static inline void fb_pixel(int x, int y, uint16_t color) {
    if ((unsigned)x < SCREEN_WIDTH && (unsigned)y < SCREEN_HEIGHT)
        fb[y * SCREEN_WIDTH + x] = SWAP16(color);
}

/* Minimal 8×16 text renderer that writes directly into the frame buffer. */
static void fb_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale) {
    if (scale < 1) scale = 1;
    if ((uint8_t)c < 32 || (uint8_t)c > 127) c = '?';

    uint16_t fg_s = SWAP16(fg);
    uint16_t bg_s = SWAP16(bg);
    const uint8_t *glyph = font8x16_data[(uint8_t)c - 32];

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int sr = 0; sr < scale; sr++) {
            int py = y + row * scale + sr;
            if ((unsigned)py >= SCREEN_HEIGHT) continue;
            uint16_t *dst = &fb[py * SCREEN_WIDTH];
            for (int col = 0; col < 8; col++) {
                uint16_t pix = (bits & (0x80 >> col)) ? fg_s : bg_s;
                for (int sc = 0; sc < scale; sc++) {
                    int px = x + col * scale + sc;
                    if ((unsigned)px < SCREEN_WIDTH)
                        dst[px] = pix;
                }
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    if (scale < 1) scale = 1;
    while (*s) {
        fb_draw_char(x, y, *s++, fg, bg, scale);
        x += 8 * scale;
    }
}

/* Send the completed frame buffer to the display in one transaction. */
static void fb_flush(void) {
    st7789_draw_buffer(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, fb);
}

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Simple pseudo-random using stdlib rand() seeded by distance */
static uint16_t car_colors[] = {
    C64_RED, C64_YELLOW, C64_CYAN, C64_WHITE,
    C64_LIGHT_GREEN, C64_ORANGE, C64_LIGHT_RED, C64_PURPLE
};
#define NUM_CAR_COLORS  (sizeof(car_colors) / sizeof(car_colors[0]))

/* Compute lap length from track data */
static uint32_t compute_lap_length(void) {
    uint32_t total = 0;
    for (int i = 0; i < (int)NUM_TRACK_SEGS; i++) {
        total += RACE_CONDITION_TRACK[i].length;
    }
    return total;
}

/* ── Initialise ──────────────────────────────────────────────────────── */
void race_condition_init(void) {
    /* Allocate frame buffer once (persists across replays) */
    if (!fb) {
        fb = malloc(FB_PIXELS * sizeof(uint16_t));
        if (!fb) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer (%u bytes)",
                     (unsigned)(FB_PIXELS * sizeof(uint16_t)));
        }
    }

    memset(&g_game, 0, sizeof(g_game));
    srand((unsigned)esp_random());
    g_game.lap          = 1;
    g_game.seg_index    = 0;
    g_game.seg_remain   = RACE_CONDITION_TRACK[0].length;
    g_game.cur_curve    = RACE_CONDITION_TRACK[0].curve;
    g_game.lap_length   = compute_lap_length();
    g_game.next_car_dist = CAR_SPAWN_DIST;

    for (int i = 0; i < MAX_OTHER_CARS; i++) {
        g_game.cars[i].active = false;
    }
}

/* ── Update ──────────────────────────────────────────────────────────── */
void race_condition_update(bool steer_left, bool steer_right, bool accelerate) {
    if (g_game.game_over) return;

    /* ── Speed ───────────────────────────────────────────────────────── */
    if (accelerate) {
        if (g_game.speed < MAX_SPEED) g_game.speed += ACCEL;
        if (g_game.speed > MAX_SPEED) g_game.speed = MAX_SPEED;
    } else {
        if (g_game.speed > 0) g_game.speed -= DECEL;
        if (g_game.speed < 0) g_game.speed = 0;
    }

    /* ── Steering ────────────────────────────────────────────────────── */
    if (steer_left)  g_game.steer_pos -= STEER_SPEED;
    if (steer_right) g_game.steer_pos += STEER_SPEED;

    /* Curve pushes car outward */
    g_game.steer_pos += g_game.cur_curve * g_game.speed / 80;

    /* Clamp lateral position */
    if (g_game.steer_pos < -ROAD_MAX_OFFSET) g_game.steer_pos = -ROAD_MAX_OFFSET;
    if (g_game.steer_pos >  ROAD_MAX_OFFSET) g_game.steer_pos =  ROAD_MAX_OFFSET;

    /* Off-road penalty: slow down when far from centre */
    int16_t abs_pos = g_game.steer_pos < 0 ? -g_game.steer_pos : g_game.steer_pos;
    if (abs_pos > 70) {
        g_game.speed = g_game.speed * 9 / 10;   /* friction */
    }

    /* ── Distance / track progression ─────────────────────────────── */
    int speed_tick = g_game.speed / 10;
    if (speed_tick < 0) speed_tick = 0;

    g_game.distance     += (uint32_t)speed_tick;
    g_game.lap_distance += (uint32_t)speed_tick;

    /* Advance through track segments */
    g_game.seg_remain -= speed_tick;
    while (g_game.seg_remain <= 0) {
        g_game.seg_index++;
        if (g_game.seg_index >= NUM_TRACK_SEGS) {
            g_game.seg_index = 0;
            g_game.lap++;
            g_game.lap_distance = 0;
        }
        g_game.seg_remain += RACE_CONDITION_TRACK[g_game.seg_index].length;
        g_game.cur_curve   = RACE_CONDITION_TRACK[g_game.seg_index].curve;
    }

    /* Road visual offset for curve rendering */
    g_game.road_offset += g_game.cur_curve * g_game.speed / 60;
    /* Decay offset towards centre */
    g_game.road_offset = g_game.road_offset * 95 / 100;
    if (g_game.road_offset >  SCREEN_WIDTH)  g_game.road_offset =  SCREEN_WIDTH;
    if (g_game.road_offset < -SCREEN_WIDTH)  g_game.road_offset = -SCREEN_WIDTH;

    /* ── Traffic spawning ─────────────────────────────────────────── */
    if (g_game.distance >= g_game.next_car_dist) {
        for (int i = 0; i < MAX_OTHER_CARS; i++) {
            if (!g_game.cars[i].active) {
                g_game.cars[i].active = true;
                g_game.cars[i].dist   = 500;  /* ahead of player */
                g_game.cars[i].lane   = (rand() % 120) - 60;
                g_game.cars[i].color  = car_colors[rand() % NUM_CAR_COLORS];
                break;
            }
        }
        g_game.next_car_dist = g_game.distance +
            (uint32_t)(CAR_SPAWN_DIST - (g_game.speed > 40 ? g_game.speed * 2 : 0));
    }

    /* ── Update traffic ───────────────────────────────────────────── */
    for (int i = 0; i < MAX_OTHER_CARS; i++) {
        if (!g_game.cars[i].active) continue;

        /* Other cars move slower than max speed, so relative motion
         * is player speed minus their constant cruise.               */
        g_game.cars[i].dist -= speed_tick;

        /* Despawn behind player */
        if (g_game.cars[i].dist < -100) {
            g_game.cars[i].active = false;
            continue;
        }

        /* Collision when car is very close and lanes overlap */
        if (g_game.cars[i].dist < 20 && g_game.cars[i].dist > -10) {
            int16_t diff = g_game.steer_pos - g_game.cars[i].lane;
            if (diff < 0) diff = -diff;
            if (diff < 26) {
                g_game.game_over = true;
                return;
            }
        }
    }

    g_game.frame++;
}

/* ── Draw (all rendering goes into the frame buffer) ─────────────────── */

/*
 * Draw a horizontal road scanline at screen row y.
 * t = 0 at bottom of road, 1 at horizon (fractional, scaled ×1000).
 */
static void draw_road_line(int y, int t1000, uint32_t scroll) {
    /* Interpolate road width */
    int road_w = ROAD_BASE_W - (ROAD_BASE_W - ROAD_TOP_W) * t1000 / 1000;
    if (road_w < 2) road_w = 2;

    /* Road centre shifts with curve offset (perspective) */
    int cx = SCREEN_WIDTH / 2 + g_game.road_offset * t1000 / 1000;

    int road_l = cx - road_w / 2;
    int road_r = cx + road_w / 2;

    /* Rumble-strip width scales with road */
    int rumble_w = road_w / 12;
    if (rumble_w < 1) rumble_w = 1;

    /* Stripe / dash selector based on scroll position and depth */
    int stripe_phase = ((int)(scroll / 4) + t1000 / (1000 / RUMBLE_STRIPE_LEN))
                       % (RUMBLE_STRIPE_LEN * 2);
    bool stripe_on = stripe_phase < RUMBLE_STRIPE_LEN;

    /* ── Colours ──────────────────────────────────────────────────── */
    uint16_t grass_col   = stripe_on ? C64_GREEN : C64_DARK_GREY;
    uint16_t rumble_col  = stripe_on ? C64_RED : C64_WHITE;
    uint16_t road_col    = stripe_on ? C64_DARK_GREY : C64_GREY;

    /* Centre dashes */
    int dash_phase = ((int)(scroll / 4) + t1000 / (1000 / CENTRE_DASH_LEN))
                     % (CENTRE_DASH_LEN * 2);
    bool dash_on = dash_phase < CENTRE_DASH_LEN;

    /* ── Draw scanline segments into frame buffer ────────────────── */
    /* Left grass */
    if (road_l > 0) {
        fb_fill_rect(0, y, road_l, 1, grass_col);
    }
    /* Left rumble strip */
    if (rumble_w > 0 && road_l >= 0 && road_l + rumble_w < SCREEN_WIDTH) {
        fb_fill_rect(road_l, y, rumble_w, 1, rumble_col);
    }
    /* Road surface */
    int inner_l = road_l + rumble_w;
    int inner_r = road_r - rumble_w;
    if (inner_l < 0) inner_l = 0;
    if (inner_r > SCREEN_WIDTH) inner_r = SCREEN_WIDTH;
    if (inner_r > inner_l) {
        fb_fill_rect(inner_l, y, inner_r - inner_l, 1, road_col);
    }
    /* Centre dash */
    if (dash_on && road_w > 20) {
        int dash_x = cx - 1;
        if (dash_x >= inner_l && dash_x + 2 <= inner_r) {
            fb_fill_rect(dash_x, y, 2, 1, C64_WHITE);
        }
    }
    /* Right rumble strip */
    if (rumble_w > 0 && road_r - rumble_w >= 0 && road_r <= SCREEN_WIDTH) {
        fb_fill_rect(road_r - rumble_w, y, rumble_w, 1, rumble_col);
    }
    /* Right grass */
    if (road_r < SCREEN_WIDTH) {
        fb_fill_rect(road_r, y, SCREEN_WIDTH - road_r, 1, grass_col);
    }
}

/*
 * Draw a small car sprite at the given screen position.
 * w controls the apparent size (perspective scaling).
 */
static void draw_car_sprite(int x, int y, int w, uint16_t body_color) {
    if (w < 3) w = 3;
    int h = w * 2 / 3;
    if (h < 2) h = 2;

    /* Clamp to screen */
    if (x - w / 2 < 0 || x + w / 2 >= SCREEN_WIDTH) return;
    if (y < HORIZON_Y || y + h >= SCREEN_HEIGHT) return;

    /* Body */
    fb_fill_rect(x - w / 2, y, w, h, body_color);
    /* Windscreen */
    int ws_w = w * 2 / 3;
    if (ws_w < 2) ws_w = 2;
    int ws_h = h / 3;
    if (ws_h < 1) ws_h = 1;
    fb_fill_rect(x - ws_w / 2, y, ws_w, ws_h, C64_LIGHT_BLUE);
}

/*
 * Draw player car at the bottom-centre of the screen.
 */
static void draw_player_car(void) {
    int car_w = 28;
    int car_h = 18;
    int car_x = SCREEN_WIDTH / 2 - g_game.steer_pos * 2 / 3;
    int car_y = SCREEN_HEIGHT - car_h - 6;

    /* Shadow */
    fb_fill_rect(car_x - car_w / 2 + 2, car_y + car_h - 2, car_w, 3, C64_DARK_GREY);

    /* Main body (classic F1 red) */
    fb_fill_rect(car_x - car_w / 2, car_y, car_w, car_h, C64_RED);

    /* Cockpit */
    int cp_w = car_w / 2;
    int cp_h = car_h / 3;
    fb_fill_rect(car_x - cp_w / 2, car_y + 2, cp_w, cp_h, C64_DARK_GREY);

    /* Nose stripe */
    fb_fill_rect(car_x - 1, car_y - 2, 3, 4, C64_WHITE);

    /* Rear wing */
    fb_fill_rect(car_x - car_w / 2 - 2, car_y + car_h - 4, car_w + 4, 2, C64_BLACK);
}

void race_condition_draw(void) {
    if (!fb) return;   /* no buffer – cannot render */

    uint32_t scroll = g_game.distance;

    /* ── Sky ──────────────────────────────────────────────────────── */
    fb_fill_rect(0, 0, SCREEN_WIDTH, HORIZON_Y, C64_LIGHT_BLUE);

    /* Distant hills / tree line silhouette (scrolls slowly) */
    {
        int hill_scroll = (int)(scroll / 20) % SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; x += 4) {
            int phase = (x + hill_scroll) % 80;
            int h;
            if (phase < 40) {
                h = phase / 4;
            } else {
                h = (80 - phase) / 4;
            }
            int tree_y = HORIZON_Y - h - 3;
            if (tree_y < 0) tree_y = 0;
            fb_fill_rect(x, tree_y, 4, HORIZON_Y - tree_y, C64_GREEN);
        }
    }

    /* ── Road scanlines ───────────────────────────────────────────── */
    int road_lines = SCREEN_HEIGHT - HORIZON_Y;
    for (int i = 0; i < road_lines; i++) {
        int y = SCREEN_HEIGHT - 1 - i;
        int t1000 = i * 1000 / road_lines;
        draw_road_line(y, t1000, scroll);
    }

    /* ── Other cars ───────────────────────────────────────────────── */
    for (int i = 0; i < MAX_OTHER_CARS; i++) {
        if (!g_game.cars[i].active) continue;
        if (g_game.cars[i].dist <= 0 || g_game.cars[i].dist > 500) continue;

        int depth = g_game.cars[i].dist;
        if (depth < 1) depth = 1;

        int scale1000 = 1000 - depth * 1000 / 500;
        if (scale1000 < 50) scale1000 = 50;

        int cy = HORIZON_Y + (SCREEN_HEIGHT - HORIZON_Y) * scale1000 / 1000;
        int cw = 8 + 20 * scale1000 / 1000;

        int cx = SCREEN_WIDTH / 2
                 + g_game.cars[i].lane * scale1000 / 1000
                 + g_game.road_offset * (1000 - scale1000) / 1000;

        draw_car_sprite(cx, cy, cw, g_game.cars[i].color);
    }

    /* ── Player car ───────────────────────────────────────────────── */
    draw_player_car();

    /* ── HUD ──────────────────────────────────────────────────────── */
    char buf[40];

    /* Speed bar */
    int bar_w = g_game.speed * 60 / MAX_SPEED;
    if (bar_w > 0) {
        uint16_t bar_col = g_game.speed > MAX_SPEED * 3 / 4 ? C64_RED : C64_YELLOW;
        fb_fill_rect(5, 3, bar_w, 5, bar_col);
    }
    snprintf(buf, sizeof(buf), "%3d km/h", g_game.speed);
    fb_draw_string(70, 1, buf, C64_WHITE, C64_LIGHT_BLUE, 1);

    /* Lap */
    snprintf(buf, sizeof(buf), "LAP %u", (unsigned)g_game.lap);
    fb_draw_string(SCREEN_WIDTH - 60, 1, buf, C64_YELLOW, C64_LIGHT_BLUE, 1);

    /* Score (distance) */
    snprintf(buf, sizeof(buf), "%lu m", (unsigned long)g_game.distance);
    fb_draw_string(SCREEN_WIDTH / 2 - 30, 1, buf, C64_WHITE, C64_LIGHT_BLUE, 1);

    /* ── Game-over overlay ────────────────────────────────────────── */
    if (g_game.game_over) {
        fb_fill_rect(40, 50, 240, 70, C64_BLACK);
        fb_draw_string(80, 58, "GAME OVER", C64_RED, C64_BLACK, 2);

        snprintf(buf, sizeof(buf), "Distance: %lu m", (unsigned long)g_game.distance);
        fb_draw_string(80, 82, buf, C64_WHITE, C64_BLACK, 1);

        snprintf(buf, sizeof(buf), "Lap %u", (unsigned)g_game.lap);
        fb_draw_string(80, 98, buf, C64_YELLOW, C64_BLACK, 1);

        fb_draw_string(72, 112, "Press any key", C64_LIGHT_GREY, C64_BLACK, 1);
    }

    /* ── Flush the completed frame to the display ────────────────── */
    fb_flush();
}

/* ── Queries ─────────────────────────────────────────────────────────── */
bool race_condition_is_active(void) {
    return !g_game.game_over;
}

uint32_t race_condition_get_score(void) {
    return g_game.distance;
}

int16_t race_condition_get_speed(void) {
    return g_game.speed;
}
