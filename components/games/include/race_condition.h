#ifndef RACE_CONDITION_H
#define RACE_CONDITION_H

#include <stdint.h>
#include <stdbool.h>

void race_condition_init(void);
void race_condition_update(bool steer_left, bool steer_right, bool accelerate);
void race_condition_draw(void);
bool race_condition_is_active(void);
uint32_t race_condition_get_score(void);
int16_t  race_condition_get_speed(void);

#endif /* RACE_CONDITION_H */
