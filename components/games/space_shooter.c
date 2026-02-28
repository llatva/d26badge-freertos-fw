#include "space_shooter.h"
#include "st7789.h"
#include "buttons.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Display dimensions
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170

// Colors
#define COLOR_SPACE     0x0000  // Black
#define COLOR_SHIP      0x07FF  // Cyan
#define COLOR_ASTEROID  0xF800  // Red
#define COLOR_BULLET    0xFFE0  // Yellow
#define COLOR_TEXT      0xFFFF  // White

// Game constants
#define SHIP_SIZE       12
#define SHIP_Y          (SCREEN_HEIGHT - 25)
#define SHIP_SPEED      6
#define ASTEROID_SIZE   16
#define ASTEROID_SPEED  3
#define BULLET_WIDTH    3
#define BULLET_HEIGHT   8
#define BULLET_SPEED    8
#define MAX_ASTEROIDS   5
#define MAX_BULLETS     3
#define FPS_DELAY_MS    16  // 60 FPS

// Game objects
typedef struct {
    int16_t x, y;
    bool active;
} bullet_t;

typedef struct {
    int16_t x, y;
    int8_t size;  // Size variation for asteroids
    bool active;
} asteroid_t;

typedef struct {
    int16_t ship_x;
    uint32_t score;
    bullet_t bullets[MAX_BULLETS];
    asteroid_t asteroids[MAX_ASTEROIDS];
    bool game_over;
    uint32_t last_spawn_time;
} game_state_t;

static game_state_t g_game;

// Draw filled rectangle using optimized bulk transfer
static void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    st7789_fill_rect(x, y, w, h, color);
}

// Draw ship (triangle pointing up)
static void draw_ship(int16_t x, int16_t y) {
    // Simple rectangular ship for performance
    draw_rect(x - SHIP_SIZE/2, y - SHIP_SIZE/2, SHIP_SIZE, SHIP_SIZE, COLOR_SHIP);
    // Cockpit
    draw_rect(x - 2, y - SHIP_SIZE/2 - 2, 4, 4, COLOR_BULLET);
}

// Draw asteroid (square with size variation)
static void draw_asteroid(int16_t x, int16_t y, int8_t size) {
    draw_rect(x - size/2, y - size/2, size, size, COLOR_ASTEROID);
}

// Draw bullet
static void draw_bullet(int16_t x, int16_t y) {
    draw_rect(x - BULLET_WIDTH/2, y, BULLET_WIDTH, BULLET_HEIGHT, COLOR_BULLET);
}

// Initialize game
void space_shooter_init(void) {
    memset(&g_game, 0, sizeof(g_game));
    g_game.ship_x = SCREEN_WIDTH / 2;
    g_game.score = 0;
    g_game.game_over = false;
    g_game.last_spawn_time = 0;
    
    // Initialize all bullets as inactive
    for (int i = 0; i < MAX_BULLETS; i++) {
        g_game.bullets[i].active = false;
    }
    
    // Initialize all asteroids as inactive
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        g_game.asteroids[i].active = false;
    }
}

// Check collision between two rectangles
static bool check_collision(int16_t x1, int16_t y1, int16_t w1, int16_t h1,
                           int16_t x2, int16_t y2, int16_t w2, int16_t h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 &&
            y1 < y2 + h2 && y1 + h1 > y2);
}

// Update game logic
void space_shooter_update(bool move_left, bool move_right, bool shoot) {
    if (g_game.game_over) return;
    
    // Move ship
    if (move_left && g_game.ship_x > SHIP_SIZE/2) {
        g_game.ship_x -= SHIP_SPEED;
    }
    if (move_right && g_game.ship_x < SCREEN_WIDTH - SHIP_SIZE/2) {
        g_game.ship_x += SHIP_SPEED;
    }
    
    // Shoot bullet
    static bool last_shoot = false;
    if (shoot && !last_shoot) {
        // Find inactive bullet slot
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!g_game.bullets[i].active) {
                g_game.bullets[i].x = g_game.ship_x;
                g_game.bullets[i].y = SHIP_Y - SHIP_SIZE/2;
                g_game.bullets[i].active = true;
                break;
            }
        }
    }
    last_shoot = shoot;
    
    // Update bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_game.bullets[i].active) {
            g_game.bullets[i].y -= BULLET_SPEED;
            if (g_game.bullets[i].y < 0) {
                g_game.bullets[i].active = false;
            }
        }
    }
    
    // Spawn new asteroids
    static uint32_t frame_count = 0;
    frame_count++;
    if (frame_count % 40 == 0) {  // Spawn every ~0.67 seconds at 60 FPS
        for (int i = 0; i < MAX_ASTEROIDS; i++) {
            if (!g_game.asteroids[i].active) {
                g_game.asteroids[i].x = rand() % (SCREEN_WIDTH - ASTEROID_SIZE) + ASTEROID_SIZE/2;
                g_game.asteroids[i].y = -ASTEROID_SIZE;
                g_game.asteroids[i].size = ASTEROID_SIZE + (rand() % 8) - 4;  // Size variation
                g_game.asteroids[i].active = true;
                break;
            }
        }
    }
    
    // Update asteroids
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_game.asteroids[i].active) {
            g_game.asteroids[i].y += ASTEROID_SPEED;
            
            // Check if asteroid hit ship
            if (check_collision(g_game.ship_x - SHIP_SIZE/2, SHIP_Y - SHIP_SIZE/2, 
                              SHIP_SIZE, SHIP_SIZE,
                              g_game.asteroids[i].x - g_game.asteroids[i].size/2, 
                              g_game.asteroids[i].y - g_game.asteroids[i].size/2,
                              g_game.asteroids[i].size, g_game.asteroids[i].size)) {
                g_game.game_over = true;
                return;
            }
            
            // Remove asteroid if off screen
            if (g_game.asteroids[i].y > SCREEN_HEIGHT + ASTEROID_SIZE) {
                g_game.asteroids[i].active = false;
            }
        }
    }
    
    // Check bullet-asteroid collisions
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_game.bullets[i].active) {
            for (int j = 0; j < MAX_ASTEROIDS; j++) {
                if (g_game.asteroids[j].active) {
                    if (check_collision(g_game.bullets[i].x - BULLET_WIDTH/2, 
                                      g_game.bullets[i].y,
                                      BULLET_WIDTH, BULLET_HEIGHT,
                                      g_game.asteroids[j].x - g_game.asteroids[j].size/2,
                                      g_game.asteroids[j].y - g_game.asteroids[j].size/2,
                                      g_game.asteroids[j].size, g_game.asteroids[j].size)) {
                        // Hit! Destroy both
                        g_game.bullets[i].active = false;
                        g_game.asteroids[j].active = false;
                        g_game.score += 10;
                        break;
                    }
                }
            }
        }
    }
}

// Draw game
void space_shooter_draw(void) {
    // Clear screen to space black
    st7789_fill(COLOR_SPACE);
    
    // Draw ship
    draw_ship(g_game.ship_x, SHIP_Y);
    
    // Draw bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_game.bullets[i].active) {
            draw_bullet(g_game.bullets[i].x, g_game.bullets[i].y);
        }
    }
    
    // Draw asteroids
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_game.asteroids[i].active) {
            draw_asteroid(g_game.asteroids[i].x, g_game.asteroids[i].y, 
                         g_game.asteroids[i].size);
        }
    }
    
    // Draw score
    char score_str[32];
    snprintf(score_str, sizeof(score_str), "Score: %lu", g_game.score);
    st7789_draw_string(5, 5, score_str, COLOR_TEXT, COLOR_SPACE, 1);
    
    // Draw game over
    if (g_game.game_over) {
        st7789_draw_string(SCREEN_WIDTH/2 - 40, SCREEN_HEIGHT/2 - 20, 
                          "GAME OVER", COLOR_TEXT, COLOR_SPACE, 2);
        st7789_draw_string(SCREEN_WIDTH/2 - 48, SCREEN_HEIGHT/2 + 10,
                          "Press B to restart", COLOR_TEXT, COLOR_SPACE, 1);
    }
}

// Check if game is active
bool space_shooter_is_active(void) {
    return !g_game.game_over;
}

// Get current score
uint32_t space_shooter_get_score(void) {
    return g_game.score;
}
