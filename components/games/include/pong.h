#ifndef PONG_H
#define PONG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize Pong game
 */
void pong_init(void);

/**
 * @brief Update game logic (call each frame)
 * @param player_up   Player paddle moving up
 * @param player_down Player paddle moving down
 */
void pong_update(bool player_up, bool player_down);

/**
 * @brief Draw the game state
 */
void pong_draw(void);

/**
 * @brief Check if game is still active (not game over)
 * @return true if game is active, false if game over
 */
bool pong_is_active(void);

/**
 * @brief Get current player score
 * @return Current score
 */
uint32_t pong_get_score(void);

/**
 * @brief Check if a point was scored this frame (for LED effects)
 * @return true if a point was scored this update
 */
bool pong_scored_this_frame(void);

#endif // PONG_H
