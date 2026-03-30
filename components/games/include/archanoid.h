#ifndef ARCHANOID_H
#define ARCHANOID_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize Archanoid game
 */
void archanoid_init(void);

/**
 * @brief Update game logic (call each frame)
 * @param move_left  Paddle moving left
 * @param move_right Paddle moving right
 * @param launch     Launch ball (when stuck to paddle)
 */
void archanoid_update(bool move_left, bool move_right, bool launch);

/**
 * @brief Draw the game state
 */
void archanoid_draw(void);

/**
 * @brief Check if game is still active (not game over)
 * @return true if game is active, false if game over
 */
bool archanoid_is_active(void);

/**
 * @brief Get current score
 * @return Current score
 */
uint32_t archanoid_get_score(void);

/**
 * @brief Check if a brick was destroyed this frame (for LED effects)
 * @return true if a brick was destroyed this update
 */
bool archanoid_hit_brick_this_frame(void);

#endif // ARCHANOID_H
