#ifndef HACKY_BIRD_H
#define HACKY_BIRD_H

#include <stdbool.h>
#include <stdint.h>

// Initialize the game state
void hacky_bird_init(void);

// Update game state (called each frame)
// flap_pressed: true if flap button was pressed this frame
void hacky_bird_update(bool flap_pressed);

// Draw the current game state
void hacky_bird_draw(void);

// Check if game is still active
bool hacky_bird_is_active(void);

// Get the final score
uint16_t hacky_bird_get_score(void);

#endif // HACKY_BIRD_H
