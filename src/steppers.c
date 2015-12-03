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

#define STEPS_PER_MM				400		// how many steps for 1 mm (depends on stepper and microstep sattings)

#define BASE_TIMER_PERIOD			50		// how often the interrupt fires (clk * 8) -- at max speed, half a step can be made on each interrupt -- lowest possible

#define STEPPER_STEPS_TO_FULL_SPEED	1024	// number of stepper steps (i.e. distance) before it can reach full speed -- better use a power of two (faster)
#define STEPPER_MIN_SPEED			10		// minimum safe speed for abrupt start and stop
#define STEPPER_MAX_SPEED			400	// stepper full speed (max. increase to the accumulator on each interrupt)
#define FIXED_POINT_OVF				256		// (half) movement occurs when accumulator overshoots this value (lower or equal to STEPPER_MAX_SPEED)

volatile int32_t stepper_speed= STEPPER_MAX_SPEED;


volatile stepper_data steppers[3];
volatile bool steppers_respect_endstop= true;

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

void stepper_init()
{
	int i;
	for(i=0; i<3; ++i)
	{
		volatile stepper_data* s = &steppers[i];
		s->source= 0;
		s->position= 0;
		s->target= 0;
		s->steps_to_full_speed= 0;
		s->fp_accu= 0;
	}
	stepper_init_hw();
	stepper_power(true);
}

void stepper_set_targets(float target_in_absolute_mm)
{
	int32_t target_in_absolute_steps= (int32_t)(2 * STEPS_PER_MM * target_in_absolute_mm);
	cli();
	// This should be called only when previous motion has finished,
	// else movement may not be smooth
	for(int axis=0; axis<3; ++axis)
	{
		volatile stepper_data* s = &steppers[axis];
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
		// Hence, speed may evolve instead as a triangle [ min -> less_than_stepper_speed -> min ]
		if(steps_to_full_speed > half_distance) steps_to_full_speed= half_distance;
		s->steps_to_full_speed= steps_to_full_speed;

	}
	sei();
}
/*
void stepper_set_target(uint8_t axis, int32_t target_in_absolute_steps)
{
	int32_t target_in_absolute_steps= (int32_t)(2 * STEPS_PER_MM * target_in_absolute_mm);
	cli();
	volatile stepper_data* s = &steppers[axis];
	if(target_in_absolute_steps > s->position)
		DIRECTION_POS(axis);
	else
		DIRECTION_NEG(axis);
	s->source= s->position;
	s->target= target_in_absolute_steps*2; // x2 because of 2-phase signal
	sei();
}
*/

int32_t stepper_get_position(uint8_t axis)
{
	return steppers[axis].position/2;
}

int stepper_get_direction(uint8_t axis)
{
	return (PORTD & (1<<(axis+5))) ? +1 : -1;
}

bool stepper_is_moving(uint8_t axis)
{
	return (steppers[axis].target - steppers[axis].position != 0);
}

bool steppers_are_moving()
{
	for(int axis=0;axis<3;++axis)
		if(steppers[axis].target - steppers[axis].position != 0)
			return true;
	return false;
}



// Stepper acceleration theory and profile:  http://www.ti.com/lit/an/slyt482/slyt482.pdf
// Todo: https://en.wikipedia.org/wiki/Smoothstep ? precomputed bicubic speed variation?

// timer1 count down
ISR(TIMER1_COMPA_vect)
{
	for(uint8_t stepper_index=0;stepper_index<3;++stepper_index)
	{
		volatile stepper_data* s = &steppers[stepper_index];

		if(steppers_respect_endstop && limit_is_hit(stepper_index))
		{
			s->target= s->position; // no move, and stop here
			print_string("endstop hit\n");
			return;
		}

		int32_t position= s->position;
		int32_t target= s->target;

		// How far are we from the target (absolute value)?
		int32_t steps_to_dest= target - position;
		if(steps_to_dest==0) continue; // already there
		uint8_t positive= (steps_to_dest>0) ? 1 : 0;
		if(!positive) steps_to_dest= -steps_to_dest; // the stepper direction was already set during stepper_set_target()

		int32_t speed;
		int32_t steps_to_full_speed= s->steps_to_full_speed;
		if(steps_to_dest < steps_to_full_speed)
			speed= STEPPER_MIN_SPEED + ((stepper_speed-STEPPER_MIN_SPEED) * steps_to_dest) / STEPPER_STEPS_TO_FULL_SPEED;
		else
		{
			// May be we are close to the source instead?
			int32_t steps_from_source= position - s->source;
			if(steps_from_source<0) steps_from_source= -steps_from_source;
			if(steps_from_source < steps_to_full_speed)
				speed= STEPPER_MIN_SPEED + ((stepper_speed-STEPPER_MIN_SPEED) * steps_from_source) / STEPPER_STEPS_TO_FULL_SPEED;
			else // No, we are far from both bounds, so run full speed (that is, when ends are far enough)
				speed= stepper_speed;
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
			PORTD ^=  1<<(stepper_index+2);	// half a step
			accu-= FIXED_POINT_OVF;
		}
		s->fp_accu= accu;
		s->position= position;
	}
}
