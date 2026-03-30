#include "archanoid.h"
#include "st7789.h"
#include "badge_settings.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Display dimensions */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170

/* Game constants */
#define PADDLE_WIDTH  40
#define PADDLE_HEIGHT  6
#define PADDLE_Y      (SCREEN_HEIGHT - 14)
#define PADDLE_SPEED   5
#define BALL_SIZE       4
#define BALL_SPEED      3

/* Brick layout */
#define BRICK_COLS    14
#define BRICK_ROWS     5
#define BRICK_WIDTH   20
#define BRICK_HEIGHT   8
#define BRICK_PAD      2
#define BRICK_OFFSET_X ((SCREEN_WIDTH - (BRICK_COLS * (BRICK_WIDTH + BRICK_PAD) - BRICK_PAD)) / 2)
#define BRICK_OFFSET_Y 24

#define TOTAL_BRICKS  (BRICK_COLS * BRICK_ROWS)
#define MAX_LIVES      3

/* C64-style colour palette (RGB565) — vibrant retro colours */
static const uint16_t C64_BRICK_COLORS[BRICK_ROWS] = {
    RGB565(255,  50,  50),   /* Red      */
    RGB565(255, 180,   0),   /* Orange   */
    RGB565(255, 255,  50),   /* Yellow   */
    RGB565( 50, 255,  50),   /* Green    */
    RGB565( 80, 120, 255),   /* Blue     */
};

/* Border colour (C64 light-blue feel) */
#define COLOR_BORDER  RGB565(120, 120, 255)
#define COLOR_BG      0x0000   /* Black */
#define COLOR_PADDLE  RGB565(160, 160, 255)
#define COLOR_BALL    0xFFFF   /* White */
#define COLOR_TEXT    0xFFFF   /* White */
#define COLOR_LIVES   RGB565(255, 80, 80)

/* Brick state */
typedef struct {
    bool alive;
} brick_t;

/* Game state */
typedef struct {
    /* Paddle */
    int16_t paddle_x;
    /* Ball */
    int16_t ball_x, ball_y;
    int16_t ball_dx, ball_dy;
    bool    ball_attached;     /* Stuck to paddle before launch */
    /* Bricks */
    brick_t bricks[BRICK_ROWS][BRICK_COLS];
    int     bricks_remaining;
    /* Score & lives */
    uint32_t score;
    uint32_t prev_score;
    uint8_t  lives;
    uint8_t  prev_lives;
    /* State */
    bool     game_over;
    bool     hit_brick_this_frame;
    bool     first_draw;
    /* Previous positions for incremental draw */
    int16_t  prev_paddle_x;
    int16_t  prev_ball_x, prev_ball_y;
} archanoid_state_t;

static archanoid_state_t g_arc;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void attach_ball(void) {
    g_arc.ball_attached = true;
    g_arc.ball_x  = g_arc.paddle_x + PADDLE_WIDTH / 2 - BALL_SIZE / 2;
    g_arc.ball_y  = PADDLE_Y - BALL_SIZE;
    g_arc.ball_dx = BALL_SPEED;
    g_arc.ball_dy = -BALL_SPEED;
}

static void init_bricks(void) {
    g_arc.bricks_remaining = TOTAL_BRICKS;
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            g_arc.bricks[r][c].alive = true;
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void archanoid_init(void) {
    memset(&g_arc, 0, sizeof(g_arc));
    g_arc.paddle_x  = SCREEN_WIDTH / 2 - PADDLE_WIDTH / 2;
    g_arc.lives     = MAX_LIVES;
    g_arc.score     = 0;
    g_arc.prev_score = 0;
    g_arc.prev_lives = MAX_LIVES;
    g_arc.game_over = false;
    g_arc.first_draw = true;
    g_arc.hit_brick_this_frame = false;
    init_bricks();
    attach_ball();
}

void archanoid_update(bool move_left, bool move_right, bool launch) {
    if (g_arc.game_over) return;

    g_arc.hit_brick_this_frame = false;

    /* --- Paddle movement --- */
    if (move_left  && g_arc.paddle_x > 0)
        g_arc.paddle_x -= PADDLE_SPEED;
    if (move_right && g_arc.paddle_x < SCREEN_WIDTH - PADDLE_WIDTH)
        g_arc.paddle_x += PADDLE_SPEED;

    /* Clamp */
    if (g_arc.paddle_x < 0) g_arc.paddle_x = 0;
    if (g_arc.paddle_x > SCREEN_WIDTH - PADDLE_WIDTH)
        g_arc.paddle_x = SCREEN_WIDTH - PADDLE_WIDTH;

    /* --- Ball attached to paddle --- */
    if (g_arc.ball_attached) {
        g_arc.ball_x = g_arc.paddle_x + PADDLE_WIDTH / 2 - BALL_SIZE / 2;
        g_arc.ball_y = PADDLE_Y - BALL_SIZE;
        if (launch) {
            g_arc.ball_attached = false;
        }
        return;
    }

    /* --- Move ball --- */
    g_arc.ball_x += g_arc.ball_dx;
    g_arc.ball_y += g_arc.ball_dy;

    /* Left / right wall bounce */
    if (g_arc.ball_x <= 0) {
        g_arc.ball_x  = 0;
        g_arc.ball_dx = -g_arc.ball_dx;
    }
    if (g_arc.ball_x >= SCREEN_WIDTH - BALL_SIZE) {
        g_arc.ball_x  = SCREEN_WIDTH - BALL_SIZE;
        g_arc.ball_dx = -g_arc.ball_dx;
    }

    /* Top wall bounce */
    if (g_arc.ball_y <= 0) {
        g_arc.ball_y  = 0;
        g_arc.ball_dy = -g_arc.ball_dy;
    }

    /* Bottom — lose life */
    if (g_arc.ball_y >= SCREEN_HEIGHT) {
        g_arc.lives--;
        if (g_arc.lives == 0) {
            g_arc.game_over = true;
        } else {
            attach_ball();
        }
        return;
    }

    /* --- Paddle collision --- */
    if (g_arc.ball_dy > 0 &&
        g_arc.ball_y + BALL_SIZE >= PADDLE_Y &&
        g_arc.ball_y + BALL_SIZE <= PADDLE_Y + PADDLE_HEIGHT &&
        g_arc.ball_x + BALL_SIZE >= g_arc.paddle_x &&
        g_arc.ball_x <= g_arc.paddle_x + PADDLE_WIDTH) {
        g_arc.ball_dy = -g_arc.ball_dy;
        /* Angle based on hit position */
        int16_t hit = (g_arc.ball_x + BALL_SIZE / 2) - (g_arc.paddle_x + PADDLE_WIDTH / 2);
        g_arc.ball_dx = hit / 4;
        if (g_arc.ball_dx == 0) g_arc.ball_dx = (rand() & 1) ? 1 : -1;
        /* Keep speed bounded */
        if (g_arc.ball_dx >  BALL_SPEED) g_arc.ball_dx =  BALL_SPEED;
        if (g_arc.ball_dx < -BALL_SPEED) g_arc.ball_dx = -BALL_SPEED;
        g_arc.ball_y = PADDLE_Y - BALL_SIZE;
    }

    /* --- Brick collisions --- */
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!g_arc.bricks[r][c].alive) continue;

            int16_t bx = BRICK_OFFSET_X + c * (BRICK_WIDTH + BRICK_PAD);
            int16_t by = BRICK_OFFSET_Y + r * (BRICK_HEIGHT + BRICK_PAD);

            /* AABB overlap test */
            if (g_arc.ball_x + BALL_SIZE > bx &&
                g_arc.ball_x < bx + BRICK_WIDTH &&
                g_arc.ball_y + BALL_SIZE > by &&
                g_arc.ball_y < by + BRICK_HEIGHT) {

                g_arc.bricks[r][c].alive = false;
                g_arc.bricks_remaining--;
                g_arc.score += (uint32_t)(BRICK_ROWS - r) * 10;
                g_arc.hit_brick_this_frame = true;

                /* Determine bounce direction */
                int16_t overlap_left   = (g_arc.ball_x + BALL_SIZE) - bx;
                int16_t overlap_right  = (bx + BRICK_WIDTH) - g_arc.ball_x;
                int16_t overlap_top    = (g_arc.ball_y + BALL_SIZE) - by;
                int16_t overlap_bottom = (by + BRICK_HEIGHT) - g_arc.ball_y;

                int16_t min_x = (overlap_left < overlap_right) ? overlap_left : overlap_right;
                int16_t min_y = (overlap_top < overlap_bottom) ? overlap_top : overlap_bottom;

                if (min_x < min_y) {
                    g_arc.ball_dx = -g_arc.ball_dx;
                } else {
                    g_arc.ball_dy = -g_arc.ball_dy;
                }

                /* Win condition */
                if (g_arc.bricks_remaining <= 0) {
                    g_arc.game_over = true;
                }
                return;  /* One brick per frame to keep it fair */
            }
        }
    }
}

void archanoid_draw(void) {
    uint16_t bg = COLOR_BG;

    /* --- First draw: full screen redraw --- */
    if (g_arc.first_draw) {
        st7789_fill(bg);

        /* Draw all alive bricks */
        for (int r = 0; r < BRICK_ROWS; r++) {
            uint16_t color = C64_BRICK_COLORS[r];
            for (int c = 0; c < BRICK_COLS; c++) {
                if (!g_arc.bricks[r][c].alive) continue;
                int16_t bx = BRICK_OFFSET_X + c * (BRICK_WIDTH + BRICK_PAD);
                int16_t by = BRICK_OFFSET_Y + r * (BRICK_HEIGHT + BRICK_PAD);
                st7789_fill_rect(bx, by, BRICK_WIDTH, BRICK_HEIGHT, color);
            }
        }

        /* Draw top border line (C64-style) */
        st7789_fill_rect(0, BRICK_OFFSET_Y - 3, SCREEN_WIDTH, 2, COLOR_BORDER);

        g_arc.prev_paddle_x = g_arc.paddle_x;
        g_arc.prev_ball_x   = g_arc.ball_x;
        g_arc.prev_ball_y   = g_arc.ball_y;
        g_arc.first_draw    = false;
    }

    /* --- Erase previous paddle & ball --- */
    st7789_fill_rect(g_arc.prev_paddle_x, PADDLE_Y,
                     PADDLE_WIDTH, PADDLE_HEIGHT, bg);
    st7789_fill_rect(g_arc.prev_ball_x, g_arc.prev_ball_y,
                     BALL_SIZE, BALL_SIZE, bg);

    /* --- Erase destroyed bricks (only those hit this frame) --- */
    if (g_arc.hit_brick_this_frame) {
        for (int r = 0; r < BRICK_ROWS; r++) {
            for (int c = 0; c < BRICK_COLS; c++) {
                if (g_arc.bricks[r][c].alive) continue;
                int16_t bx = BRICK_OFFSET_X + c * (BRICK_WIDTH + BRICK_PAD);
                int16_t by = BRICK_OFFSET_Y + r * (BRICK_HEIGHT + BRICK_PAD);
                /* Only erase if we see the blank spot hasn't been drawn yet:
                 * We can simply draw all dead bricks as bg each frame they die.
                 * The cost is small since only one brick dies per frame. */
                st7789_fill_rect(bx, by, BRICK_WIDTH, BRICK_HEIGHT, bg);
            }
        }
    }

    /* --- Draw paddle --- */
    st7789_fill_rect(g_arc.paddle_x, PADDLE_Y,
                     PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_PADDLE);

    /* --- Draw ball --- */
    st7789_fill_rect(g_arc.ball_x, g_arc.ball_y,
                     BALL_SIZE, BALL_SIZE, COLOR_BALL);

    /* Save positions for next frame */
    g_arc.prev_paddle_x = g_arc.paddle_x;
    g_arc.prev_ball_x   = g_arc.ball_x;
    g_arc.prev_ball_y   = g_arc.ball_y;

    /* --- Score / lives HUD — only redraw when changed --- */
    if (g_arc.score != g_arc.prev_score || g_arc.lives != g_arc.prev_lives) {
        char buf[32];
        /* Score */
        st7789_fill_rect(0, 2, 120, 16, bg);
        snprintf(buf, sizeof(buf), "SCORE %lu", g_arc.score);
        st7789_draw_string(4, 2, buf, COLOR_TEXT, bg, 1);

        /* Lives */
        st7789_fill_rect(SCREEN_WIDTH - 80, 2, 80, 16, bg);
        snprintf(buf, sizeof(buf), "LIVES %u", g_arc.lives);
        st7789_draw_string(SCREEN_WIDTH - 76, 2, buf, COLOR_LIVES, bg, 1);

        g_arc.prev_score = g_arc.score;
        g_arc.prev_lives = g_arc.lives;
    }

    /* --- Game over / win overlay --- */
    if (g_arc.game_over) {
        if (g_arc.bricks_remaining <= 0) {
            st7789_draw_string(SCREEN_WIDTH / 2 - 56, SCREEN_HEIGHT / 2 - 20,
                               "LEVEL CLEAR!", COLOR_TEXT, bg, 2);
        } else {
            st7789_draw_string(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 20,
                               "GAME OVER", COLOR_TEXT, bg, 2);
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "Score: %lu", g_arc.score);
        st7789_draw_string(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 + 10,
                           buf, COLOR_TEXT, bg, 1);
        st7789_draw_string(SCREEN_WIDTH / 2 - 48, SCREEN_HEIGHT / 2 + 30,
                           "Press B to exit", COLOR_TEXT, bg, 1);
        g_arc.first_draw = true;  /* Reset for next game */
    }
}

bool archanoid_is_active(void) {
    return !g_arc.game_over;
}

uint32_t archanoid_get_score(void) {
    return g_arc.score;
}

bool archanoid_hit_brick_this_frame(void) {
    return g_arc.hit_brick_this_frame;
}
