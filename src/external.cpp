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

void external_init() 
{
	// Start, reset, feed hold
	CONTROL_DDR   &= ~(CONTROL_MASK); 		// Configure as input pins
	CONTROL_PORT  |= CONTROL_MASK;  		// Enable internal pull-up resistors. Normal high operation.
	CONTROL_PCMSK |= CONTROL_MASK;  		// Enable specific pins of the Pin Change Interrupt
	PCICR         |= (1 << CONTROL_INT);    // Enable Pin Change Interrupt TODO: we may only need raising edge, not change!!

	EXT_ENDSTOP_DDR |= (1 << EXT_ENDSTOP_BIT);
	external_endstop(false);
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
// only the realtime command execute variable to have the main program execute these when 
// its ready. This works exactly like the character-based realtime commands when picked off
// directly from the incoming serial data stream.
ISR(CONTROL_INT_vect) 
{
	uint8_t pin = (CONTROL_PIN & CONTROL_MASK);
	#ifdef CONTROL_INVERT_MASK
		pin ^= CONTROL_INVERT_MASK;
	#endif

		serial_write( (pin & (1<<EXT_STEP_BIT)) ? 1 : 0 );

	// Enter only if any CONTROL pin is detected as active.
	if(pin)
	{
		// The master sent a step pulse
		if(pin & (1<<EXT_STEP_BIT))
		{
			if(external_mode) // only in "external mode" (=E, vs. default =C configuration mode)
			{
				if(CONTROL_PIN & (1<<EXT_DIR_BIT))
					DIRECTION_ALL_ON();
				else
					DIRECTION_ALL_OFF();
				STEPPER_ALL_HALF_STEP(); // pulse lengths, see https://www.pololu.com/product/2133
				delay_us(2); // 1.9us for DRV8825 and 1us for A4988
				STEPPER_ALL_HALF_STEP();
			}
		}
	}
}
