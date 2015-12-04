/*
 * steppers.h
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#ifndef STEPPERS_H_
#define STEPPERS_H_

extern bool steppers_relative_mode;

typedef struct stepper_data
{
	int32_t source;		// source of current movement
	int32_t position;	// position within [source,target]
	int32_t target;		// target position
	int32_t ramp_length;
	uint16_t fp_accu;	// fixed point accumulator
} stepper_data;

extern volatile int32_t stepper_speed;
extern volatile stepper_data steppers[3];
extern volatile bool steppers_respect_endstop;

void stepper_init();

void stepper_zero(uint8_t axis);
void steppers_zero();

void stepper_power(bool s);
bool stepper_are_powered();
void stepper_set_targets(float mm, float speed_factor);
void steppers_zero_speed();

void stepper_set_target(uint8_t axis, float mm, float speed_factor);
bool stepper_is_moving(uint8_t axis);
bool steppers_are_moving();

float stepper_get_position(uint8_t axis);
int stepper_get_direction(uint8_t axis);

// ================= high level calls =================

uint8_t move_modal(float pos, float speed_factor);
uint8_t move_modal_axis(uint8_t axis, float pos, float speed_factor);

void set_origin();
void set_origin_single(uint8_t axis);

#endif /* STEPPERS_H_ */
