/*
 * limits.h
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#ifndef LIMITS_H_
#define LIMITS_H_

extern volatile uint8_t limits_sticky_states;

void limits_enable();
void limits_disable();
void limits_init();
uint8_t limits_get_current_states();
bool limit_is_hit(int axis);


#endif /* LIMITS_H_ */
