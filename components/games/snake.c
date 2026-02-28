#include "snake.h"
#include "st7789.h"
#include "buttons.h"
#include "sk6812.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Display dimensions
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170

// Colors
#define COLOR_BG      0x0000  // Black
#define COLOR_SNAKE   0x07E0  // Green
#define COLOR_HEAD    0x07FF  // Cyan
#define COLOR_FOOD    0xF800  // Red
#define COLOR_TEXT    0xFFFF  // White

// Game constants
#define GRID_SIZE     10      // Size of each grid cell in pixels
#define GRID_WIDTH    (SCREEN_WIDTH / GRID_SIZE)   // 32 cells
#define GRID_HEIGHT   (SCREEN_HEIGHT / GRID_SIZE)  // 17 cells
#define MAX_SNAKE_LEN (GRID_WIDTH * GRID_HEIGHT)
#define INITIAL_SPEED 150     // Initial delay in ms
#define MIN_SPEED     50      // Maximum speed (min delay)
#define SPEED_INCREASE 5      // Speed increase per food

// Snake segment
typedef struct {
    int16_t x, y;
} point_t;

// Game state
typedef struct {
    point_t snake[MAX_SNAKE_LEN];
    uint16_t length;
    snake_direction_t direction;
    snake_direction_t next_direction;  // Buffered input
    point_t food;
    uint32_t score;
    uint32_t last_score;  // Track score changes for display
    uint32_t speed_delay;
    bool game_over;
    bool ate_food_this_frame;  // Flag for LED effect
} game_state_t;

static game_state_t g_game;

// Draw a grid cell
static void draw_cell(int16_t x, int16_t y, uint16_t color) {
    st7789_fill_rect(x * GRID_SIZE, y * GRID_SIZE, GRID_SIZE - 1, GRID_SIZE - 1, color);
}

// Spawn food at random location (not on snake)
static void spawn_food(void) {
    bool valid = false;
    while (!valid) {
        g_game.food.x = rand() % GRID_WIDTH;
        g_game.food.y = rand() % GRID_HEIGHT;
        
        // Check if food is on snake
        valid = true;
        for (uint16_t i = 0; i < g_game.length; i++) {
            if (g_game.snake[i].x == g_game.food.x && 
                g_game.snake[i].y == g_game.food.y) {
                valid = false;
                break;
            }
        }
    }
}

// Initialize game
void snake_init(void) {
    memset(&g_game, 0, sizeof(g_game));
    
    // Start snake in center
    g_game.length = 3;
    g_game.snake[0].x = GRID_WIDTH / 2;
    g_game.snake[0].y = GRID_HEIGHT / 2;
    g_game.snake[1].x = g_game.snake[0].x - 1;
    g_game.snake[1].y = g_game.snake[0].y;
    g_game.snake[2].x = g_game.snake[1].x - 1;
    g_game.snake[2].y = g_game.snake[1].y;
    
    g_game.direction = SNAKE_DIR_RIGHT;
    g_game.next_direction = SNAKE_DIR_RIGHT;
    g_game.score = 0;
    g_game.speed_delay = INITIAL_SPEED;
    g_game.game_over = false;
    
    spawn_food();
}

// Handle direction input (prevent 180-degree turns)
void snake_set_direction(snake_direction_t dir) {
    // Can't reverse direction
    if ((dir == SNAKE_DIR_UP && g_game.direction != SNAKE_DIR_DOWN) ||
        (dir == SNAKE_DIR_DOWN && g_game.direction != SNAKE_DIR_UP) ||
        (dir == SNAKE_DIR_LEFT && g_game.direction != SNAKE_DIR_RIGHT) ||
        (dir == SNAKE_DIR_RIGHT && g_game.direction != SNAKE_DIR_LEFT)) {
        g_game.next_direction = dir;
    }
}

// Update game logic
void snake_update(void) {
    if (g_game.game_over) return;
    
    // Clear the ate_food flag
    g_game.ate_food_this_frame = false;
    
    // Apply buffered direction change
    g_game.direction = g_game.next_direction;
    
    // Calculate new head position
    point_t new_head = g_game.snake[0];
    switch (g_game.direction) {
        case SNAKE_DIR_UP:    new_head.y--; break;
        case SNAKE_DIR_DOWN:  new_head.y++; break;
        case SNAKE_DIR_LEFT:  new_head.x--; break;
        case SNAKE_DIR_RIGHT: new_head.x++; break;
    }
    
    // Check wall collision
    if (new_head.x < 0 || new_head.x >= GRID_WIDTH ||
        new_head.y < 0 || new_head.y >= GRID_HEIGHT) {
        g_game.game_over = true;
        return;
    }
    
    // Check self collision
    for (uint16_t i = 0; i < g_game.length; i++) {
        if (g_game.snake[i].x == new_head.x && g_game.snake[i].y == new_head.y) {
            g_game.game_over = true;
            return;
        }
    }
    
    // Check food collision
    bool ate_food = (new_head.x == g_game.food.x && new_head.y == g_game.food.y);
    
    if (ate_food) {
        // Set flag for LED effect
        g_game.ate_food_this_frame = true;
        
        // Grow snake
        g_game.score += 10;
        
        // Increase speed
        if (g_game.speed_delay > MIN_SPEED) {
            g_game.speed_delay -= SPEED_INCREASE;
            if (g_game.speed_delay < MIN_SPEED) {
                g_game.speed_delay = MIN_SPEED;
            }
        }
        
        // Move snake segments forward and add new head
        if (g_game.length < MAX_SNAKE_LEN) {
            for (int16_t i = g_game.length; i > 0; i--) {
                g_game.snake[i] = g_game.snake[i - 1];
            }
            g_game.snake[0] = new_head;
            g_game.length++;
            
            spawn_food();
        }
    } else {
        // Move snake: shift all segments forward
        for (uint16_t i = g_game.length - 1; i > 0; i--) {
            g_game.snake[i] = g_game.snake[i - 1];
        }
        g_game.snake[0] = new_head;
    }
}

// Draw game (optimized to reduce flickering)
void snake_draw(void) {
    static bool first_draw = true;
    
    // On first draw, clear entire screen
    if (first_draw) {
        st7789_fill(COLOR_BG);
        first_draw = false;
    }
    
    // Only redraw changed cells to avoid flickering
    static point_t old_tail;
    static point_t old_food;
    static bool has_old_tail = false;
    
    // Erase old tail position (only if snake didn't grow)
    if (has_old_tail && !g_game.ate_food_this_frame) {
        draw_cell(old_tail.x, old_tail.y, COLOR_BG);
    }
    
    // Erase old food position if it was eaten
    if (g_game.ate_food_this_frame) {
        draw_cell(old_food.x, old_food.y, COLOR_BG);
    }
    
    // Draw snake body (second-to-last segment in green)
    if (g_game.length > 1) {
        draw_cell(g_game.snake[1].x, g_game.snake[1].y, COLOR_SNAKE);
    }
    
    // Draw snake head (new position in cyan)
    draw_cell(g_game.snake[0].x, g_game.snake[0].y, COLOR_HEAD);
    
    // Draw food
    draw_cell(g_game.food.x, g_game.food.y, COLOR_FOOD);
    
    // Save positions for next frame
    old_tail = g_game.snake[g_game.length - 1];
    old_food = g_game.food;
    has_old_tail = true;
    
    // Only redraw score when it changes
    if (g_game.score != g_game.last_score) {
        // Clear old score area
        st7789_fill_rect(0, 0, 100, 16, COLOR_BG);
        
        // Draw new score
        char score_str[32];
        snprintf(score_str, sizeof(score_str), "Score: %lu", g_game.score);
        st7789_draw_string(5, 5, score_str, COLOR_TEXT, COLOR_BG, 1);
        
        g_game.last_score = g_game.score;
    }
    
    // Draw game over (full screen clear needed)
    if (g_game.game_over) {
        st7789_fill(COLOR_BG);
        
        // Redraw snake for game over screen
        for (uint16_t i = 1; i < g_game.length; i++) {
            draw_cell(g_game.snake[i].x, g_game.snake[i].y, COLOR_SNAKE);
        }
        draw_cell(g_game.snake[0].x, g_game.snake[0].y, COLOR_HEAD);
        
        // Draw score
        char score_str[32];
        snprintf(score_str, sizeof(score_str), "Score: %lu", g_game.score);
        st7789_draw_string(5, 5, score_str, COLOR_TEXT, COLOR_BG, 1);
        
        st7789_draw_string(SCREEN_WIDTH/2 - 40, SCREEN_HEIGHT/2 - 20, 
                          "GAME OVER", COLOR_TEXT, COLOR_BG, 2);
        st7789_draw_string(SCREEN_WIDTH/2 - 48, SCREEN_HEIGHT/2 + 10,
                          "Press B to exit", COLOR_TEXT, COLOR_BG, 1);
        
        first_draw = true;  // Reset for next game
    }
}

// Check if game is active
bool snake_is_active(void) {
    return !g_game.game_over;
}

// Get current score
uint32_t snake_get_score(void) {
    return g_game.score;
}

// Get current speed delay (for game loop timing)
uint32_t snake_get_speed_delay(void) {
    return g_game.speed_delay;
}

// Check if food was eaten this frame (for LED effects)
bool snake_ate_food_this_frame(void) {
    return g_game.ate_food_this_frame;
}
