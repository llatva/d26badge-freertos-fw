#include "hacky_bird.h"
#include "st7789.h"
#include "buttons.h"
#include "badge_settings.h"
#include "sk6812.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Display dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 170

// Game constants
#define BIRD_X 60
#define BIRD_SIZE 8
#define PIPE_WIDTH 30
#define PIPE_GAP 60
#define PIPE_SPACING 120
#define GRAVITY 1
#define FLAP_STRENGTH -8
#define GAME_SPEED 5  // Increased for 60 FPS (was 3 at 30 FPS)
#define FPS_DELAY_MS 16  // ~60 FPS for smooth action gameplay

// Colors
#define COLOR_BIRD 0xFFE0      // Yellow
#define COLOR_PIPE 0x07E0      // Green
#define COLOR_GROUND 0xA514    // Brown
#define COLOR_SKY 0x5D1F       // Sky blue
#define COLOR_TEXT 0xFFFF      // White

// Game state
typedef struct {
    int16_t bird_y;
    int16_t bird_velocity;
    int16_t pipes[3][2];  // [pipe_index][x, gap_y] - use signed for proper scrolling
    uint16_t score;
    bool active;
    uint32_t frame_count;
    uint16_t last_scored_pipe;  // Track which pipe we last scored on
} game_state_t;

static game_state_t g_game;

// Initialize game state
static void game_init(void) {
    g_game.bird_y = SCREEN_HEIGHT / 2;
    g_game.bird_velocity = 0;
    g_game.score = 0;
    g_game.active = true;
    g_game.frame_count = 0;
    g_game.last_scored_pipe = 999;  // Invalid pipe index

    // Initialize pipes
    for (int i = 0; i < 3; i++) {
        g_game.pipes[i][0] = SCREEN_WIDTH + i * PIPE_SPACING;
        g_game.pipes[i][1] = 40 + (rand() % 60);  // Random gap position
    }
}

// Draw filled rectangle (uses optimized st7789_fill_rect for bulk SPI transfer)
static void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    st7789_fill_rect(x, y, w, h, color);
}

// Draw bird
static void draw_bird(void) {
    draw_rect(BIRD_X - BIRD_SIZE/2, g_game.bird_y - BIRD_SIZE/2, 
              BIRD_SIZE, BIRD_SIZE, COLOR_BIRD);
}

// Draw pipe
static void draw_pipe(int16_t x, int16_t gap_y) {
    // Top pipe
    if (x < SCREEN_WIDTH && x + PIPE_WIDTH > 0) {
        int16_t x_start = x < 0 ? 0 : x;
        int16_t x_end = (x + PIPE_WIDTH) > SCREEN_WIDTH ? SCREEN_WIDTH : (x + PIPE_WIDTH);
        int16_t width = x_end - x_start;
        
        if (width > 0) {
            draw_rect(x_start, 0, width, gap_y - PIPE_GAP/2, COLOR_PIPE);
            // Bottom pipe
            draw_rect(x_start, gap_y + PIPE_GAP/2, width, 
                     SCREEN_HEIGHT - (gap_y + PIPE_GAP/2), COLOR_PIPE);
        }
    }
}

// Check collision
static bool check_collision(void) {
    // Ground/ceiling collision
    if (g_game.bird_y - BIRD_SIZE/2 <= 0 || 
        g_game.bird_y + BIRD_SIZE/2 >= SCREEN_HEIGHT) {
        return true;
    }

    // Pipe collision
    for (int i = 0; i < 3; i++) {
        int16_t pipe_x = g_game.pipes[i][0];
        int16_t gap_y = g_game.pipes[i][1];

        // Check if bird is horizontally aligned with pipe
        if (pipe_x < BIRD_X + BIRD_SIZE/2 && pipe_x + PIPE_WIDTH > BIRD_X - BIRD_SIZE/2) {
            // Check if bird hits top or bottom pipe
            if (g_game.bird_y - BIRD_SIZE/2 < gap_y - PIPE_GAP/2 ||
                g_game.bird_y + BIRD_SIZE/2 > gap_y + PIPE_GAP/2) {
                return true;
            }
        }
    }

    return false;
}

// Update game state
void hacky_bird_update(bool flap_pressed) {
    // Handle flap
    if (flap_pressed) {
        g_game.bird_velocity = FLAP_STRENGTH;
    }

    // Apply gravity
    g_game.bird_velocity += GRAVITY;
    g_game.bird_y += g_game.bird_velocity;

    // Update pipes
    for (int i = 0; i < 3; i++) {
        int16_t old_x = g_game.pipes[i][0];
        g_game.pipes[i][0] -= GAME_SPEED;

        // Check if bird just passed through gate
        // Bird passed when pipe goes from right of bird to left of bird
        if (old_x >= BIRD_X && g_game.pipes[i][0] < BIRD_X && i != g_game.last_scored_pipe) {
            // Check if bird is within the gap
            int16_t gap_y = g_game.pipes[i][1];
            if (g_game.bird_y > gap_y - PIPE_GAP/2 && g_game.bird_y < gap_y + PIPE_GAP/2) {
                g_game.score++;
                g_game.last_scored_pipe = i;
                
                // LED bar effect on gate pass - flash green
                sk6812_color_t green = {0, 255, 0};
                sk6812_fill(sk6812_scale(green, 60));
                sk6812_show();
            }
        }

        // Respawn pipe when it goes off screen
        if (g_game.pipes[i][0] + PIPE_WIDTH < 0) {
            g_game.pipes[i][0] = SCREEN_WIDTH;
            g_game.pipes[i][1] = 40 + (rand() % 60);
            // Reset last scored when pipe respawns
            if (i == g_game.last_scored_pipe) {
                g_game.last_scored_pipe = 999;
            }
        }
    }

    // Check collision
    if (check_collision()) {
        g_game.active = false;
        
        // Flash red LEDs on death
        sk6812_color_t red = {255, 0, 0};
        sk6812_fill(sk6812_scale(red, 60));
        sk6812_show();
    }

    g_game.frame_count++;
}

// Draw game
void hacky_bird_draw(void) {
    // Clear screen with sky color
    st7789_fill(COLOR_SKY);

    // Draw pipes
    for (int i = 0; i < 3; i++) {
        draw_pipe(g_game.pipes[i][0], g_game.pipes[i][1]);
    }

    // Draw bird
    draw_bird();

    // Draw score
    char score_str[16];
    snprintf(score_str, sizeof(score_str), "Score: %d", g_game.score);
    st7789_draw_string(10, 10, score_str, COLOR_TEXT, COLOR_SKY, 1);
}

// Check if game is still active
bool hacky_bird_is_active(void) {
    return g_game.active;
}

// Get final score
uint16_t hacky_bird_get_score(void) {
    return g_game.score;
}

// Initialize the game
void hacky_bird_init(void) {
    game_init();
}
