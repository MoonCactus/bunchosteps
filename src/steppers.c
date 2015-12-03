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

#define STEPPER_STEPS_TO_FULL_SPEED	600		// number of stepper steps (i.e. distance) before it can reach full speed
#define STEPPER_MIN_SPEED			100		// minimum safe speed for abrupt start and stop
#define STEPPER_MAX_SPEED			1500	// stepper full speed (increase to the accumulator at each interrupt)
#define FIXED_POINT					1500	// (half) movement occurs when accumulator overshoots this value (lower or equal to STEPPER_MAX_SPEED)
#define BASE_TIMER_PERIOD			125		// how often the interrupt fires (clk * 8)

stepper_data steppers[3];

#define DIRECTION_POS(axis)  PORTD |=  (1<<(axis+5))
#define DIRECTION_NEG(axis)  PORTD &= ~(1<<(axis+5))
#define HALF_STEP(axis)      PORTD ^=   1<<(axis+2)


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
		s->fp_accu= 0;
	}
	stepper_init_hw();
	stepper_power(true);
}

void stepper_set_target(uint8_t axis, int32_t target_in_absolute_steps)
{
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

int32_t stepper_get_position(uint8_t axis)
{
	return steppers[axis].position/2;
}

bool stepper_is_moving(uint8_t axis)
{
	return (steppers[axis].target - steppers[axis].position != 0);
}

// Stepper acceleration theory and profile:  http://www.ti.com/lit/an/slyt482/slyt482.pdf
// Todo: https://en.wikipedia.org/wiki/Smoothstep ? precomputed bicubic speed variation?

// timer1 count down
ISR(TIMER1_COMPA_vect)
{
	for(int i=0;i<3;++i)
	{
		stepper_data* s = &steppers[i];

		if(limit_is_hit(i))
		{
			s->target= s->position; // stop here
			print_string("endstop hit\n");
			return;
		}

		// How far are we from the target?
		int32_t steps_to_dest= s->target - s->position;
		if(steps_to_dest==0) continue; // already there
		if(steps_to_dest<0) steps_to_dest= -steps_to_dest; // direction was set already in stepper_set_target()

		// Set speed according to the distance from the bounds
		int32_t speed;
		if(steps_to_dest < STEPPER_STEPS_TO_FULL_SPEED)
			speed= STEPPER_MIN_SPEED
				+ ((STEPPER_MAX_SPEED-STEPPER_MIN_SPEED) * steps_to_dest)
				 / STEPPER_STEPS_TO_FULL_SPEED;
		else
		{
			// How far are we from the source?
			int32_t steps_from_source= s->position - s->source;
			if(steps_from_source<0) steps_from_source= -steps_from_source;
			if(steps_from_source < STEPPER_STEPS_TO_FULL_SPEED)
				speed= STEPPER_MIN_SPEED
					+ ((STEPPER_MAX_SPEED-STEPPER_MIN_SPEED) * steps_from_source)
					/ STEPPER_STEPS_TO_FULL_SPEED;
			else // far from both bounds, so run full speed
				speed= STEPPER_MAX_SPEED;
		}

		// Accumulate and do the movement
		s->fp_accu+= speed;
		while(s->fp_accu > FIXED_POINT) // "while" should not happen :/
		{
			s->fp_accu-= FIXED_POINT;
			if(s->target - s->position>0)
				++s->position;
			else
				--s->position;
			HALF_STEP(X_STEP_BIT+i);
		}
	}
}
