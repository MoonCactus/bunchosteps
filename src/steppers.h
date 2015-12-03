/*
 * steppers.h
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#ifndef STEPPERS_H_
#define STEPPERS_H_


typedef struct stepper_data
{
	int32_t source;		// source of current movement
	volatile int32_t position;	// position within [source,target]
	int32_t target;		// target position
	uint16_t fp_accu;	// fixed point accumulator
} stepper_data;

extern volatile int32_t stepper_speed;
extern volatile stepper_data steppers[3];
extern volatile bool steppers_respect_endstop;

void stepper_init();
void stepper_power(bool s);
void stepper_set_targets(int32_t target_in_absolute_steps);
void stepper_set_target(uint8_t axis, int32_t target_in_absolute_steps);
bool stepper_is_moving(uint8_t axis);
bool steppers_are_moving();

int32_t stepper_get_position(uint8_t axis);
int stepper_get_direction(uint8_t axis);

#endif /* STEPPERS_H_ */
