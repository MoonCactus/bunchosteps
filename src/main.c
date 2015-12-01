/*
  main.c - An embedded CNC Controller with rs274/ngc (g-code) support
  Part of Grbl
  
  Copyright (c) 2011-2015 Sungeun K. Jeon
  Copyright (c) 2009-2011 Simen Svale Skogsrud
  
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

#include <avr/io.h>
#include <stdbool.h>
#include <avr/interrupt.h>

int main(void)
{
	cli();
	// Initialize system upon power-up.
	serial_init();   // Setup serial baud rate and interrupts
	millis_init();
	stepper_init();

	serial_reset_read_buffer(); // Clear serial read buffer
	print_pstr("Y:booted\n");

	sei(); // Enable timers

	uint64_t m= millis();
	int cycle_len=3000;
	while(1)
	{
		command_collect();

		uint64_t m2= millis();
		if(m2-m>cycle_len)
		{
			serial_write('!');
			m= m+cycle_len;
		}
	}

	return 0;   /* Never reached */
}

