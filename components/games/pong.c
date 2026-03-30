#include "pong.h"
#include "st7789.h"
#include "badge_settings.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Display dimensions */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170

/* Game constants */
#define PADDLE_WIDTH   4
#define PADDLE_HEIGHT  30
#define PADDLE_MARGIN  10
#define PADDLE_SPEED   4
#define BALL_SIZE       4
#define BALL_INIT_DX    3
#define BALL_INIT_DY    2
#define AI_SPEED        3
#define NET_DASH_LEN    4
#define NET_GAP_LEN     6
#define WINNING_SCORE   7

/* Game state */
typedef struct {
    /* Paddles (y = top of paddle) */
    int16_t player_y;
    int16_t ai_y;
    /* Ball */
    int16_t ball_x, ball_y;
    int16_t ball_dx, ball_dy;
    /* Scores */
    uint32_t player_score;
    uint32_t ai_score;
    /* State flags */
    bool     game_over;
    bool     scored_this_frame;  /* For LED effect */
    bool     first_draw;
    /* Previous positions for incremental draw */
    int16_t  prev_player_y;
    int16_t  prev_ai_y;
    int16_t  prev_ball_x, prev_ball_y;
    uint32_t prev_player_score;
    uint32_t prev_ai_score;
} pong_state_t;

static pong_state_t g_pong;

/* Reset ball to centre, random direction */
static void reset_ball(void) {
    g_pong.ball_x  = SCREEN_WIDTH / 2;
    g_pong.ball_y  = SCREEN_HEIGHT / 2;
    g_pong.ball_dx = (rand() & 1) ? BALL_INIT_DX : -BALL_INIT_DX;
    g_pong.ball_dy = (rand() % 3) - 1;  /* -1, 0, or 1 */
    if (g_pong.ball_dy == 0) g_pong.ball_dy = (rand() & 1) ? 1 : -1;
}

void pong_init(void) {
    memset(&g_pong, 0, sizeof(g_pong));
    g_pong.player_y = SCREEN_HEIGHT / 2 - PADDLE_HEIGHT / 2;
    g_pong.ai_y     = SCREEN_HEIGHT / 2 - PADDLE_HEIGHT / 2;
    g_pong.player_score = 0;
    g_pong.ai_score     = 0;
    g_pong.game_over    = false;
    g_pong.first_draw   = true;
    g_pong.scored_this_frame = false;
    g_pong.prev_player_score = 0;
    g_pong.prev_ai_score     = 0;
    reset_ball();
}

void pong_update(bool player_up, bool player_down) {
    if (g_pong.game_over) return;

    g_pong.scored_this_frame = false;

    /* --- Player paddle movement --- */
    if (player_up && g_pong.player_y > 0)
        g_pong.player_y -= PADDLE_SPEED;
    if (player_down && g_pong.player_y < SCREEN_HEIGHT - PADDLE_HEIGHT)
        g_pong.player_y += PADDLE_SPEED;

    /* Clamp */
    if (g_pong.player_y < 0) g_pong.player_y = 0;
    if (g_pong.player_y > SCREEN_HEIGHT - PADDLE_HEIGHT)
        g_pong.player_y = SCREEN_HEIGHT - PADDLE_HEIGHT;

    /* --- AI paddle movement --- */
    int16_t ai_center = g_pong.ai_y + PADDLE_HEIGHT / 2;
    if (g_pong.ball_y < ai_center - 2)
        g_pong.ai_y -= AI_SPEED;
    else if (g_pong.ball_y > ai_center + 2)
        g_pong.ai_y += AI_SPEED;

    /* Clamp */
    if (g_pong.ai_y < 0) g_pong.ai_y = 0;
    if (g_pong.ai_y > SCREEN_HEIGHT - PADDLE_HEIGHT)
        g_pong.ai_y = SCREEN_HEIGHT - PADDLE_HEIGHT;

    /* --- Move ball --- */
    g_pong.ball_x += g_pong.ball_dx;
    g_pong.ball_y += g_pong.ball_dy;

    /* Top/bottom wall bounce */
    if (g_pong.ball_y <= 0) {
        g_pong.ball_y  = 0;
        g_pong.ball_dy = -g_pong.ball_dy;
    }
    if (g_pong.ball_y >= SCREEN_HEIGHT - BALL_SIZE) {
        g_pong.ball_y  = SCREEN_HEIGHT - BALL_SIZE;
        g_pong.ball_dy = -g_pong.ball_dy;
    }

    /* Player paddle collision (left side) */
    if (g_pong.ball_dx < 0 &&
        g_pong.ball_x <= PADDLE_MARGIN + PADDLE_WIDTH &&
        g_pong.ball_x >= PADDLE_MARGIN &&
        g_pong.ball_y + BALL_SIZE >= g_pong.player_y &&
        g_pong.ball_y <= g_pong.player_y + PADDLE_HEIGHT) {
        g_pong.ball_dx = -g_pong.ball_dx;
        /* Add spin based on paddle hit position */
        int16_t hit_pos = (g_pong.ball_y + BALL_SIZE / 2) - (g_pong.player_y + PADDLE_HEIGHT / 2);
        g_pong.ball_dy  = hit_pos / 4;
        if (g_pong.ball_dy == 0) g_pong.ball_dy = (rand() & 1) ? 1 : -1;
        g_pong.ball_x = PADDLE_MARGIN + PADDLE_WIDTH + 1;
    }

    /* AI paddle collision (right side) */
    if (g_pong.ball_dx > 0 &&
        g_pong.ball_x + BALL_SIZE >= SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH &&
        g_pong.ball_x + BALL_SIZE <= SCREEN_WIDTH - PADDLE_MARGIN &&
        g_pong.ball_y + BALL_SIZE >= g_pong.ai_y &&
        g_pong.ball_y <= g_pong.ai_y + PADDLE_HEIGHT) {
        g_pong.ball_dx = -g_pong.ball_dx;
        int16_t hit_pos = (g_pong.ball_y + BALL_SIZE / 2) - (g_pong.ai_y + PADDLE_HEIGHT / 2);
        g_pong.ball_dy  = hit_pos / 4;
        if (g_pong.ball_dy == 0) g_pong.ball_dy = (rand() & 1) ? 1 : -1;
        g_pong.ball_x = SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH - BALL_SIZE - 1;
    }

    /* --- Scoring --- */
    if (g_pong.ball_x < 0) {
        /* AI scores */
        g_pong.ai_score++;
        g_pong.scored_this_frame = true;
        if (g_pong.ai_score >= WINNING_SCORE) {
            g_pong.game_over = true;
        } else {
            reset_ball();
        }
    } else if (g_pong.ball_x + BALL_SIZE > SCREEN_WIDTH) {
        /* Player scores */
        g_pong.player_score++;
        g_pong.scored_this_frame = true;
        if (g_pong.player_score >= WINNING_SCORE) {
            g_pong.game_over = true;
        } else {
            reset_ball();
        }
    }
}

void pong_draw(void) {
    uint16_t fg = settings_get_accent_color();
    uint16_t txt = settings_get_text_color();
    uint16_t bg = COLOR_BLACK;

    if (g_pong.first_draw) {
        st7789_fill(bg);
        g_pong.prev_player_y = g_pong.player_y;
        g_pong.prev_ai_y     = g_pong.ai_y;
        g_pong.prev_ball_x   = g_pong.ball_x;
        g_pong.prev_ball_y   = g_pong.ball_y;
        g_pong.first_draw    = false;

        /* Draw centre net */
        for (int16_t y = 0; y < SCREEN_HEIGHT; y += NET_DASH_LEN + NET_GAP_LEN) {
            int16_t h = NET_DASH_LEN;
            if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
            st7789_fill_rect(SCREEN_WIDTH / 2 - 1, y, 2, h, fg);
        }
    }

    /* Erase previous positions */
    st7789_fill_rect(PADDLE_MARGIN, g_pong.prev_player_y,
                     PADDLE_WIDTH, PADDLE_HEIGHT, bg);
    st7789_fill_rect(SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH, g_pong.prev_ai_y,
                     PADDLE_WIDTH, PADDLE_HEIGHT, bg);
    st7789_fill_rect(g_pong.prev_ball_x, g_pong.prev_ball_y,
                     BALL_SIZE, BALL_SIZE, bg);

    /* Redraw centre net segments that may have been erased by ball */
    {
        int16_t net_x = SCREEN_WIDTH / 2 - 1;
        /* Only redraw net segments near the ball's previous position */
        for (int16_t y = 0; y < SCREEN_HEIGHT; y += NET_DASH_LEN + NET_GAP_LEN) {
            int16_t h = NET_DASH_LEN;
            if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
            /* Check overlap with previous ball position */
            if (g_pong.prev_ball_x + BALL_SIZE >= net_x &&
                g_pong.prev_ball_x <= net_x + 2 &&
                g_pong.prev_ball_y + BALL_SIZE >= y &&
                g_pong.prev_ball_y <= y + h) {
                st7789_fill_rect(net_x, y, 2, h, fg);
            }
        }
    }

    /* Draw paddles */
    st7789_fill_rect(PADDLE_MARGIN, g_pong.player_y,
                     PADDLE_WIDTH, PADDLE_HEIGHT, fg);
    st7789_fill_rect(SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH, g_pong.ai_y,
                     PADDLE_WIDTH, PADDLE_HEIGHT, fg);

    /* Draw ball */
    st7789_fill_rect(g_pong.ball_x, g_pong.ball_y,
                     BALL_SIZE, BALL_SIZE, txt);

    /* Save positions for next frame */
    g_pong.prev_player_y = g_pong.player_y;
    g_pong.prev_ai_y     = g_pong.ai_y;
    g_pong.prev_ball_x   = g_pong.ball_x;
    g_pong.prev_ball_y   = g_pong.ball_y;

    /* Score display — only redraw when changed */
    if (g_pong.player_score != g_pong.prev_player_score ||
        g_pong.ai_score != g_pong.prev_ai_score) {
        char buf[16];
        /* Player score (left of centre) */
        snprintf(buf, sizeof(buf), "%lu", g_pong.player_score);
        st7789_fill_rect(SCREEN_WIDTH / 2 - 40, 4, 24, 16, bg);
        st7789_draw_string(SCREEN_WIDTH / 2 - 40, 4, buf, txt, bg, 2);

        /* AI score (right of centre) */
        snprintf(buf, sizeof(buf), "%lu", g_pong.ai_score);
        st7789_fill_rect(SCREEN_WIDTH / 2 + 20, 4, 24, 16, bg);
        st7789_draw_string(SCREEN_WIDTH / 2 + 20, 4, buf, txt, bg, 2);

        g_pong.prev_player_score = g_pong.player_score;
        g_pong.prev_ai_score     = g_pong.ai_score;
    }

    /* Game over overlay */
    if (g_pong.game_over) {
        const char *result = (g_pong.player_score >= WINNING_SCORE) ? "YOU WIN!" : "YOU LOSE";
        st7789_draw_string(SCREEN_WIDTH / 2 - 32, SCREEN_HEIGHT / 2 - 20,
                           result, txt, bg, 2);
        st7789_draw_string(SCREEN_WIDTH / 2 - 48, SCREEN_HEIGHT / 2 + 10,
                           "Press B to exit", txt, bg, 1);
        g_pong.first_draw = true;  /* Reset for next game */
    }
}

bool pong_is_active(void) {
    return !g_pong.game_over;
}

uint32_t pong_get_score(void) {
    return g_pong.player_score;
}

bool pong_scored_this_frame(void) {
    return g_pong.scored_this_frame;
}
