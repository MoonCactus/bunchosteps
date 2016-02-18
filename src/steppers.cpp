/*
 * steppers.c
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

/*
D11  Z- endstop
D10  Y- endstop
D9   X- endstop

D8   EN

D7   Z-DIR
D6   Y-DIR
D5   X-DIR

D4   Z-STEP
D3   Y-STEP
D2   Z-STEP

Ports:
B (digital pin 8 to 13)
C (analog input pins)
D (digital pins 0 to 7)
*/
#include "main.h"
#include "steppers.h"
#include "limits.h"
#include "serial.h"

#define STEPS_PER_MM				400		// how many steps for 1 mm (depends on stepper and microstep settings)
#define ENFORCE_SHARED_LIMITS				// undefine to have the steppers check only their respective limit when moving (probably unsafe)

#define BASE_TIMER_PERIOD			64		// how often the interrupt fires (clk * 8) -- at max speed, half a step can be made on each interrupt -- lowest possible

#define STEPPER_STEPS_TO_FULL_SPEED	1024	// number of stepper steps (i.e. distance) before it can reach full speed -- better use a power of two (faster)
#define STEPPER_MIN_SPEED			30		// minimum safe speed for abrupt start and stop
#define STEPPER_MAX_SPEED			140		// stepper full speed (max. increase to the accumulator on each interrupt)
//#define STEPPER_MAX_SPEED			200		// stepper full speed (max. increase to the accumulator on each interrupt)
#define FIXED_POINT_OVF				256		// (half) movement occurs when accumulator overshoots this value (higher or equal to STEPPER_MAX_SPEED)

bool steppers_relative_mode= false;

volatile int32_t stepper_speed= STEPPER_MAX_SPEED;	// todo: individual stepper speeds

volatile stepper_data steppers[3];
volatile bool steppers_respect_endstop= true;		// todo: individual settings (bitfield)

#define DIRECTION_POS(a)  PORTD |=  (1<<((a)+5))
#define DIRECTION_NEG(a)  PORTD &= ~(1<<((a)+5))

void stepper_init_hw()
{
	DDRD |=  0b11111100;  // sets Arduino pins 2-7 as outputs (3 for steps and 3 for dirs)
	DDRB |=  0b00000001;  // sets Arduino pin 8 as output (enable)

	// set up Timer 1 for stepper movement
	TCCR1A= 0;						// normal operation
	TCCR1B= bit(WGM12) | bit(CS11);	// CTC, no pre-scaling 1/8 (CS10 would be 1:1)
	OCR1A=  BASE_TIMER_PERIOD;		// compare A register value (N * clock speed)
	TIMSK1= bit (OCIE1A);			// interrupt on Compare A Match
}

void stepper_power(bool s)
{
	if(s)
		STEPPERS_DISABLE_PORT &= ~(1<<STEPPERS_DISABLE_BIT);
	else
		STEPPERS_DISABLE_PORT |=  (1<<STEPPERS_DISABLE_BIT);
}

bool stepper_are_powered()
{
	return !(STEPPERS_DISABLE_PIN & (1<<STEPPERS_DISABLE_BIT));
}

void stepper_zero(uint8_t axis)
{
	uint8_t sreg= SREG;
	cli();
	volatile stepper_data* s = &steppers[axis];
	s->source= 0;
	s->position= 0;
	s->target= 0;
	s->ramp_length= 0;
	s->fp_accu= 0;
	SREG= sreg;
}

void steppers_zero()
{
	uint8_t sreg= SREG;
	cli();
	for(uint8_t axis=0; axis<3; ++axis)
		stepper_zero(axis);
	SREG= sreg;
}

void stepper_settle_here(uint8_t axis)
{
	uint8_t sreg= SREG;
	cli();
	volatile stepper_data* s = &steppers[axis];
	s->target= s->position;
	SREG= sreg;
}

void steppers_settle_here()
{
	uint8_t sreg= SREG;
	cli();
	for(uint8_t axis=0; axis<3; ++axis)
	{
		volatile stepper_data* s = &steppers[axis];
		s->target= s->position;
	}
	SREG= sreg;
}

void stepper_init()
{
	steppers_zero();
	stepper_init_hw();
}

void stepper_set_targets(float mm, float speed_factor)
{
	// Better call only when idle else the movement will not be smooth
	uint8_t sreg= SREG;
	cli();
	for(int axis=0; axis<3; ++axis)
		stepper_set_target(axis, mm, speed_factor);
	SREG= sreg;
}

// make sure steppers restart at slow speed (ie. mostly after a limit stop is leveraged)
void steppers_zero_speed()
{
	uint8_t sreg= SREG;
	cli();
	Backup<bool> srm(steppers_relative_mode, false);
	for(int axis=0; axis<3; ++axis)
	{
		float target= float(steppers[axis].target) / (2 * STEPS_PER_MM);
		stepper_set_target(axis, target, 0.5);
	}
	SREG= sreg;
}

bool stepper_set_target(uint8_t axis, float mm, float speed_factor)
{
	if(external_mode) return false;

	uint8_t sreg= SREG;
	cli();

	int32_t target_in_absolute_steps= (int32_t)(mm * 2 * STEPS_PER_MM);


	volatile stepper_data* s = &steppers[axis];

	stepper_speed= STEPPER_MAX_SPEED * speed_factor;

	if(steppers_relative_mode)
		target_in_absolute_steps+= s->position;

	if(target_in_absolute_steps > s->position)
		DIRECTION_POS(axis);
	else
		DIRECTION_NEG(axis);
	s->source= s->position;
	s->target= target_in_absolute_steps; // x2 because of 2-phase signal

	// How long will we be accelerating? This depends on the distance between the bounds
	int32_t steps_to_full_speed= STEPPER_STEPS_TO_FULL_SPEED; // default length for acceleration and for deceleration (i.e. during which speed will change)
	// Default speed evolves as a capped triangle:
	//   [ min -> stepper_speed -> stepper_speed -> max ],
	// where stepper_speed is reached after (steps_to_full_speed) steps.

	// ...but we do not want to overshoot the middle position while accelerating,
	int32_t half_distance= (s->target - s->source)/2; if(half_distance<0) half_distance= -half_distance;
	// ...as we would not be able to slow down and halt as smoothly as we started!
	// Hence, speed evolution may become a triangle instead (max speed not reached): [ min -> less_than_stepper_speed -> min ]
	if(steps_to_full_speed > half_distance) steps_to_full_speed= half_distance;
	s->ramp_length= steps_to_full_speed;

	SREG= sreg;
	return true;
 }

float stepper_get_position(uint8_t axis)
{
	return (float)steppers[axis].position / (2 * STEPS_PER_MM);
}

void stepper_override_position(uint8_t axis, float mm)
{
	uint8_t sreg= SREG;
	steppers[axis].position= (2 * STEPS_PER_MM) * mm;
	stepper_settle_here(axis);
	SREG= sreg;
}


int stepper_get_direction(uint8_t axis)
{
	return (PORTD & (1<<(axis+5))) ? +1 : -1;
}

bool stepper_is_moving(uint8_t axis)
{
	if(steppers_respect_endstop && (sticky_limits & (1<<axis)))
		return false;
	return (steppers[axis].target - steppers[axis].position != 0);
}

bool steppers_are_moving()
{
	for(int axis=0;axis<3;++axis)
	{
		if(steppers_respect_endstop && sticky_limits)
			return false;
		if(steppers[axis].target - steppers[axis].position != 0)
			return true;
	}
	return false;
}



// Stepper acceleration theory and profile:  http://www.ti.com/lit/an/slyt482/slyt482.pdf
// Todo: https://en.wikipedia.org/wiki/Smoothstep ? precomputed bicubic speed variation?

// timer1 count down
ISR(TIMER1_COMPA_vect)
{
	if(nmi_reset) return;
	if(external_mode) return; // steppers are commanded directly by the master
	for(uint8_t stepper_index=0;stepper_index<3;++stepper_index)
	{
		volatile stepper_data* s = &steppers[stepper_index];

#ifdef ENFORCE_SHARED_LIMITS
		if(steppers_respect_endstop && sticky_limits)
#else
		if(steppers_respect_endstop && sticky_limit_is_hit(stepper_index))
#endif
		{
			// s->target= s->position; // no move, and stop here
			return;
		}

		int32_t position= s->position;
		int32_t target= s->target;
		// int32_t speed= s->stepper_speed;  --> speed the stepper can reach at max (may be less than stepper_speed when steps_to_full_speed<STEPPER_STEPS_TO_FULL_SPEED!)

		// How far are we from the target (absolute value)?
		int32_t steps_to_dest= target - position;
		if(steps_to_dest==0) continue; // already there
		uint8_t positive= (steps_to_dest>0) ? 1 : 0;
		if(!positive) steps_to_dest= -steps_to_dest; // the stepper direction was already set during stepper_set_target()

		int32_t speed;
		int32_t steps_to_full_speed= s->ramp_length;
		if(steps_to_dest < steps_to_full_speed)
			speed= STEPPER_MIN_SPEED + ((stepper_speed-STEPPER_MIN_SPEED) * steps_to_dest) / steps_to_full_speed;
		else
		{
			// May be we are close to the source instead?
			int32_t steps_from_source= position - s->source;
			if(steps_from_source<0) steps_from_source= -steps_from_source;
			if(steps_from_source < steps_to_full_speed)
				speed= STEPPER_MIN_SPEED + ((stepper_speed-STEPPER_MIN_SPEED) * steps_from_source) / STEPPER_STEPS_TO_FULL_SPEED;
			else // No, we are far from both bounds, so run full speed (that is, when ends are far enough)
				speed= stepper_speed * s->ramp_length / STEPPER_STEPS_TO_FULL_SPEED; // may be less than full speed if ramp is too short
		}

		// Accumulate and do the movement
		uint16_t accu= s->fp_accu;
		accu+= speed;
		// TODO: multiplex the 3 axes in the while loop instead of doing them sequentially, or use a faster interrupt again
		while(accu >= FIXED_POINT_OVF) // if the best world and for more regular timings, there would be no "while", just a "if"
		{
			if(positive)
				++position;
			else
				--position;
			STEPPER_HALF_STEP(stepper_index);
			accu-= FIXED_POINT_OVF;
		}
		s->fp_accu= accu;
		s->position= position;
	}
}

//
// ================= high level calls =================
//

uint8_t move_modal(float pos, float speed_factor)
{
	stepper_set_targets(pos, speed_factor);
	while(!nmi_reset && steppers_are_moving());
	return !nmi_reset && sticky_limits == 0;
}

uint8_t move_modal_axis(uint8_t axis, float pos, float speed_factor)
{
	stepper_set_target(axis, pos, speed_factor);
	while(!nmi_reset && stepper_is_moving(axis));
	return !nmi_reset && sticky_limits == 0;
}

void set_origin()
{
	uint8_t sreg= SREG;
	for(uint8_t axis=0; axis<3; ++axis)
		stepper_zero(axis);
	sticky_limits= 0;
	SREG= sreg;
}

void set_origin_single(uint8_t axis)
{
	stepper_zero(axis);
	sticky_limits &= ~(1<<(axis+1));
}


