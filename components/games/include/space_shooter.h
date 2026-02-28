#ifndef SPACE_SHOOTER_H
#define SPACE_SHOOTER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize Space Shooter game
 */
void space_shooter_init(void);

/**
 * @brief Update game logic
 * @param move_left Move ship left
 * @param move_right Move ship right
 * @param shoot Fire bullet
 */
void space_shooter_update(bool move_left, bool move_right, bool shoot);

/**
 * @brief Draw the game state
 */
void space_shooter_draw(void);

/**
 * @brief Check if game is still active (not game over)
 * @return true if game is active, false if game over
 */
bool space_shooter_is_active(void);

/**
 * @brief Get current score
 * @return Current score
 */
uint32_t space_shooter_get_score(void);

#endif // SPACE_SHOOTER_H
