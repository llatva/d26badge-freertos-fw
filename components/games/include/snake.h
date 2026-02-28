#ifndef SNAKE_H
#define SNAKE_H

#include <stdint.h>
#include <stdbool.h>

// Directions for snake_set_direction
typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_RIGHT,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT
} snake_direction_t;

/**
 * @brief Initialize Snake game
 */
void snake_init(void);

/**
 * @brief Set snake direction (buffered input to prevent 180-degree turns)
 * @param dir New direction
 */
void snake_set_direction(snake_direction_t dir);

/**
 * @brief Update game logic (call once per game tick)
 */
void snake_update(void);

/**
 * @brief Draw the game state
 */
void snake_draw(void);

/**
 * @brief Check if game is still active (not game over)
 * @return true if game is active, false if game over
 */
bool snake_is_active(void);

/**
 * @brief Get current score
 * @return Current score
 */
uint32_t snake_get_score(void);

/**
 * @brief Get current speed delay for game loop timing
 * @return Delay in milliseconds
 */
uint32_t snake_get_speed_delay(void);

/**
 * @brief Check if food was eaten this frame (for LED effects)
 * @return true if food was eaten this update
 */
bool snake_ate_food_this_frame(void);

#endif // SNAKE_H
