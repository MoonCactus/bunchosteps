/*
  system.c - Handles system level commands and real-time processes
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

// Get the external stepper select value 0,1,2, and 3 for all axes simulteanously)
#define MUX_SEL_GET_VALUE ((SEL_MUX_PIN & SEL_MUX_MASK)>>SEL_MUX_MASK_SHIFT)

void external_init() 
{
	// Start, reset, feed hold
	CONTROL_DDR   &= ~(CONTROL_MASK); 		// Configure as input pins
	CONTROL_PORT  |= CONTROL_MASK;  		// Enable internal pull-up resistors. Normal high operation.
	CONTROL_PCMSK |= CONTROL_MASK;  		// Enable specific pins of the Pin Change Interrupt
	PCICR         |= (1 << CONTROL_INT);    // Enable Pin Change Interrupt TODO: use raising edge, not change, and set asynchronous duration ourselves?

	SEL_MUX_DDR  &= ~SEL_MUX_MASK;			// Set as input pins
	SEL_MUX_PORT &= ~SEL_MUX_MASK;			// Disable internal pull-ups (due to pin13 and its led)

	EXT_ENDSTOP_DDR |= (1 << EXT_ENDSTOP_BIT);
	external_endstop(false);

#ifdef DEFAULTS_TO_EXTERNAL_MODE
	stepper_power(true);
#endif
}


void external_endstop(bool state)
{
	// Marlin: endstop is triggered with a low state
	if(state)
		EXT_ENDSTOP_PORT &= ~(1 << EXT_ENDSTOP_BIT); // clear
	else
		EXT_ENDSTOP_PORT |= (1 << EXT_ENDSTOP_BIT); // set
}

// Pin change interrupt for pin-out commands, i.e. cycle start, feed hold, and reset. Sets
// only the real-time command execute variable to have the main program execute these when
// its ready. This works exactly like the character-based real-time commands when picked off
// directly from the incoming serial data stream.
ISR(CONTROL_INT_vect) 
{
	uint8_t pin = (CONTROL_PIN & CONTROL_MASK);
	#ifdef CONTROL_INVERT_MASK
		pin ^= CONTROL_INVERT_MASK;
	#endif

	// The master sent a step pulse

		// direction
	if(CONTROL_PIN & (1<<EXT_DIR_BIT))
		DIRECTION_ALL_ON();
	else
		DIRECTION_ALL_OFF();

	// step pulse (grouped or individual)
	uint8_t axis= MUX_SEL_GET_VALUE;
	if(axis==SEL_MUX_AXIS_ALL) // ref. TRIBED_AXIS_xxx in Tribed Marlin
	{
		if(CONTROL_PIN & (1<<EXT_STEP_BIT))
			STEPPER_ALL_SET();
		else
			STEPPER_ALL_CLEAR();
	}
	else // individual axis 0,1 or 2
	{
		if(CONTROL_PIN & (1<<EXT_STEP_BIT))
		{
			STEPPER_SET();
		}
		else
			STEPPER_CLEAR();
	}
}

