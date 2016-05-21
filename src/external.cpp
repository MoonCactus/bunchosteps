/*
  external.cpp - Handles system level commands and real-time processes
  Part of Grbl

  Copyright (c) 2014-2015 Sungeun K. Jeon  

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "main.h"
#include "steppers.h"
#include "external.h"

#include "serial.h"

#define POLOLU_DIRECTION_DELAY_US 1 // delay between setting direction and sending puls
#define POLOLU_PULSE_DURATION_US  3 // length of pulse (1.9us for DRV8825 and 1.0us for A4988)

// Incoming !enable signal state
#define RT_DISABLED(pin)  (pin & (1<<RESET_BIT))
// Incoming direction signal state
#define RT_DIRECTION(pin) (pin & (1<<EXT_DIR_BIT))
// Incoming step signal state
#define RT_STEP(pin)      (pin & (1<<EXT_STEP_BIT))
// Get the external stepper select value 0,1,2, and 3 for all axes simultaneously)
#define RT_MUX            ((SEL_MUX_PIN & SEL_MUX_MASK)>>SEL_MUX_MASK_SHIFT)

uint8_t external_mode= 0;
uint8_t external_axis= 3;

static uint8_t prev_pulse_state= 0;
static uint8_t prev_direction_state= 0;

#if defined(REDUCE_JITTER_DELTA) && (REDUCE_JITTER_DELTA > 0)
	uint8_t jitter_counter= 0-REDUCE_JITTER_DELTA;
#endif

void external_init()
{
	// Start, reset, feed hold
	CONTROL_DDR		&= ~(CONTROL_MASK); 	// Configure as input pins
	CONTROL_PORT	|= CONTROL_MASK;  		// Enable internal pull-up resistors. Normal high operation.
	CONTROL_PCMSK	|= CONTROL_MASK;  		// Enable specific pins of the Pin Change Interrupt
	#ifndef USE_EXT_POLLING
	PCICR			|= (1 << CONTROL_INT);	// Enable Pin Change Interrupt TODO: use raising edge, not change, and set asynchronous duration ourselves?
	#endif

	SEL_MUX_DDR  &= ~SEL_MUX_MASK;			// Set as input pins
	SEL_MUX_PORT &= ~SEL_MUX_MASK;			// Disable internal pull-ups (due to pin13 and its led)

	EXT_ENDSTOP_DDR |= (1 << EXT_ENDSTOP_BIT);
	set_external_endstop(false);

	#ifdef DEFAULTS_TO_EXTERNAL_MODE
		stepper_power(true);
	#endif
}

uint8_t is_external_stepper_mode()
{
	return external_mode;
}

void set_external_axes(uint8_t axis)
{
	external_axis= axis;
}


void set_external_stepper_mode(bool state)
{
	if(state)
	{
		stepper_internal_interrupts(false);
		external_mode= 1;
		external_axis= 3;
	}
	else
	{
		external_mode= 0;
		stepper_internal_interrupts(true);
	}
}

void set_external_endstop(bool state)
{
	// Marlin: endstop is triggered with a low state
	if(state)
		EXT_ENDSTOP_PORT &= ~(1 << EXT_ENDSTOP_BIT); // clear
	else
		EXT_ENDSTOP_PORT |= (1 << EXT_ENDSTOP_BIT); // set
}

#ifdef USE_EXT_POLLING
void external_poll_z()
#else
// Pin change interrupt for pin-out commands, i.e. cycle start, feed hold, reset aka steps and dir
ISR(CONTROL_INT_vect) 
#endif
{
	// The master sent a signal
	#ifdef CONTROL_INVERT_MASK
		uint8_t pin = (CONTROL_PIN & CONTROL_MASK) ^ CONTROL_INVERT_MASK;
	#else
		uint8_t pin = (CONTROL_PIN & CONTROL_MASK);
	#endif

	// Check axis direction state
	#if defined(REDUCE_JITTER_DELTA) && (REDUCE_JITTER_DELTA > 0)
		counter+= REDUCE_JITTER_DELTA;
		if(!counter)
	#endif
	{
		uint8_t d= RT_DIRECTION(pin);
		if(d!=prev_direction_state)
		{
			prev_direction_state= d;
			if(d)
				DIRECTION_ALL_ON();
			else
				DIRECTION_ALL_OFF();
		}
	}

	// step pulse
	uint8_t p= RT_STEP(pin);
	if(p!=prev_pulse_state)
	{
		prev_pulse_state= p;

		uint8_t axis= RT_MUX;
		if(axis==SEL_MUX_AXIS_ALL) // ref. TRIBED_AXIS_xxx in Tribed Marlin
		{
			if(p)
				STEPPER_ALL_SET();
			else
				STEPPER_ALL_CLEAR();
		}
		else // individual axis 0,1 or 2
		{
			if(p)
				STEPPER_SET(axis);
			else
				STEPPER_CLEAR(axis);
		}
	}
}
