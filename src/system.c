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


void system_init() 
{
  CONTROL_DDR &= ~(CONTROL_MASK); // Configure as input pins
  CONTROL_PORT |= CONTROL_MASK;   // Enable internal pull-up resistors. Normal high operation.
  CONTROL_PCMSK |= CONTROL_MASK;  // Enable specific pins of the Pin Change Interrupt
  PCICR |= (1 << CONTROL_INT);    // Enable Pin Change Interrupt
}

// Pin change interrupt for pin-out commands, i.e. cycle start, feed hold, and reset. Sets
// only the realtime command execute variable to have the main program execute these when 
// its ready. This works exactly like the character-based realtime commands when picked off
// directly from the incoming serial data stream.
ISR(CONTROL_INT_vect) 
{
  uint8_t pin = (CONTROL_PIN & CONTROL_MASK);
  #ifndef INVERT_ALL_CONTROL_PINS
    pin ^= CONTROL_INVERT_MASK;
  #endif
  // Enter only if any CONTROL pin is detected as active.
  if (pin) {
	  /*
    if (bit_istrue(pin,bit(RESET_BIT)))
      {mc_reset();}
    else if (bit_istrue(pin,bit(CYCLE_START_BIT)))
      bit_true(sys_rt_exec_state, EXEC_CYCLE_START);
    #ifndef ENABLE_SAFETY_DOOR_INPUT_PIN
    else if (bit_istrue(pin,bit(FEED_HOLD_BIT)))
        bit_true(sys_rt_exec_state, EXEC_FEED_HOLD); 
    #else
    else if (bit_istrue(pin,bit(SAFETY_DOOR_BIT)))
        bit_true(sys_rt_exec_state, EXEC_SAFETY_DOOR);
    #endif
    */
  }
}
