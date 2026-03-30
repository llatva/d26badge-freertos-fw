#ifndef MONZA_H
#define MONZA_H

#include <stdint.h>
#include <stdbool.h>

void monza_init(void);
void monza_update(bool steer_left, bool steer_right, bool accelerate);
void monza_draw(void);
bool monza_is_active(void);
uint32_t monza_get_score(void);
int16_t  monza_get_speed(void);

#endif /* MONZA_H */
