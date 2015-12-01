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

typedef struct stepper_data
{
	int32_t source;		// source of current movement
	int32_t position;	// position within [source,target]
	int32_t target;		// target position
	uint32_t accu;		// accumulator TODO: uint16_t and check OVH in assembler
} stepper_data;

stepper_data steppers[3];

void stepper_init_hw()
{
	uint8_t sreg = SREG;
	cli();

	// Configure directions and status
	STEP_DDR      |= _BV(X_STEP_BIT)      | _BV(Y_STEP_BIT)      | _BV(Z_STEP_BIT);
	DIRECTION_DDR |= _BV(X_DIRECTION_BIT) | _BV(Y_DIRECTION_BIT) | _BV(Z_DIRECTION_BIT);

	STEP_PORT = 0xFF;

	// set compare match register to desired timer count:
	OCR1A = 8000;
	// turn on CTC mode:
	TCCR1B |= (1 << WGM12);
	// No prescaling (fastest clock rate):
	TCCR1B |= (1 << CS10);
	TCCR1B |= (0 << CS11);
	TCCR1B |= (0 << CS12);
	// enable timer compare interrupt:
	TIMSK1 |= (1 << OCIE1A);

	SREG= sreg;
}

void stepper_init()
{
	int i;
	for(i=0; i<3; ++i)
	{
		stepper_data* s = &steppers[i];
		s->source= 0;
		s->position= 0;
		s->target= 0;
		s->accu= 0;
	}
	stepper_init_hw();
}

#define STEP_HALF_MOVE(step_bit)  STEP_PORT = STEP_PORT ^ (1<<(step_bit))

#define STEPPER_STEPS_TO_FULL_SPEED  1000
#define STEPPER_MIN_ACCELERATION     5
#define STEPPER_MAX_ACCELERATION     100

// Stepper acceleration theory and profile:  http://www.ti.com/lit/an/slyt482/slyt482.pdf
// Todo: https://en.wikipedia.org/wiki/Smoothstep ? precomputed bicubic speed variation?

// timer1 count down
ISR(TIMER1_COMPA_vect)
{
	int i;
	for(i=0; i<3; ++i)
	{
		stepper_data* s = &steppers[i];

		// How far are we to the target?
		int32_t delta_pos= s->target - s->position;
		if(delta_pos==0) continue; // already there

		// Set movement direction
		if(delta_pos>0)
			bset(DIRECTION_DDR, X_DIRECTION_BIT+i);
		else
			bclr(DIRECTION_DDR, X_DIRECTION_BIT+i);

		// Set speed according to the distance from the bounds
		int16_t delta_speed;
		if(s->position - s->source < STEPPER_STEPS_TO_FULL_SPEED)
			delta_speed= ((STEPPER_MAX_ACCELERATION-STEPPER_MIN_ACCELERATION) * (s->position - s->source)) / STEPPER_STEPS_TO_FULL_SPEED;
		else if(delta_pos < STEPPER_STEPS_TO_FULL_SPEED)
			delta_speed= ((STEPPER_MAX_ACCELERATION-STEPPER_MIN_ACCELERATION) * delta_pos) / STEPPER_STEPS_TO_FULL_SPEED;
		else // full speed
			delta_speed= (STEPPER_MAX_ACCELERATION-STEPPER_MIN_ACCELERATION);

		delta_speed+= STEPPER_MIN_ACCELERATION; //avoid stalling!

		// Accumulate and do the movement
		s->accu+= delta_speed;
		if(s->accu & 0x10000) // overflow
		{
			s->accu &= 0xFFFF;
			if(delta_pos>0)
				s->position++;
			else
				--s->position;
			STEP_HALF_MOVE(X_STEP_BIT+i);
		}

	}
}
