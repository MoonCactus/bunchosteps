/*
  cpu_map_atmega328p.h - CPU and pin mapping configuration file
  Part of Grbl

  Copyright (c) 2012-2015 Sungeun K. Jeon

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

/* Grbl officially supports the Arduino Uno, but the other supplied pin mappings are
   supplied by users, so your results may vary. This cpu_map file serves as a central
   pin mapping settings file for AVR 328p used on the Arduino Uno.  */
   
#ifdef GRBL_PLATFORM
#error "cpu_map already defined: GRBL_PLATFORM=" GRBL_PLATFORM
#endif

#define GRBL_PLATFORM "Atmega328p"

// OK/ Define serial port pins and interrupt vectors.
#define SERIAL_RX     USART_RX_vect		// USART Rx Complete
#define SERIAL_UDRE   USART_UDRE_vect	// USART, Data Register Empty

// OK/ Define step pulse output pins. NOTE: All step bit pins must be on the same port.
#define STEP_DDR        DDRD
#define STEP_PORT       PORTD
#define X_STEP_BIT      2  // Uno Digital Pin 2
#define Y_STEP_BIT      3  // Uno Digital Pin 3
#define Z_STEP_BIT      4  // Uno Digital Pin 4
#define STEP_MASK       ((1<<X_STEP_BIT)|(1<<Y_STEP_BIT)|(1<<Z_STEP_BIT)) // All step bits

// OK/ Define step direction output pins. NOTE: All direction pins must be on the same port.
#define DIRECTION_DDR   DDRD
#define DIRECTION_PORT  PORTD
#define X_DIRECTION_BIT 5  // Uno Digital Pin 5
#define Y_DIRECTION_BIT 6  // Uno Digital Pin 6
#define Z_DIRECTION_BIT 7  // Uno Digital Pin 7
#define DIRECTION_MASK  ((1<<X_DIRECTION_BIT)|(1<<Y_DIRECTION_BIT)|(1<<Z_DIRECTION_BIT)) // All direction bits

// OK/ Define stepper driver enable/disable output pin.
#define STEPPERS_DISABLE_DDR    DDRB
#define STEPPERS_DISABLE_PORT   PORTB
#define STEPPERS_DISABLE_PIN    PINB
#define STEPPERS_DISABLE_BIT    0  // Uno Digital Pin 8
#define STEPPERS_DISABLE_MASK   (1<<STEPPERS_DISABLE_BIT)

// Define homing/hard limit switch input pins and limit interrupt vectors. 
// NOTE: All limit bit pins must be on the same port, but not on a port with other input pins (CONTROL).
#define LIMIT_DDR        DDRB
#define LIMIT_PIN        PINB
#define LIMIT_PORT       PORTB
#define LIMIT_MASK_SHIFT 1
#define X_LIMIT_BIT      1  // Uno Digital Pin 9
#define Y_LIMIT_BIT      2  // Uno Digital Pin 10
#define Z_LIMIT_BIT      3  // Uno Digital Pin 11
#define LIMIT_MASK       ((1<<X_LIMIT_BIT)|(1<<Y_LIMIT_BIT)|(1<<Z_LIMIT_BIT)) // All limit bits
#define LIMIT_INT        PCIE0  // Pin change interrupt enable pin
#define LIMIT_INT_vect   PCINT0_vect 
#define LIMIT_PCMSK      PCMSK0 // Pin change interrupt register

// Define "stepper select" multiplexed pins
#define SEL_MUX_DDR      DDRB
#define SEL_MUX_PIN      PINB
#define SEL_MUX_PORT     PORTB
#define SEL_MUX_MASK_SHIFT 4
#define SEL_MUX0_BIT     4  // Uno Digital Pin 12
#define SEL_MUX1_BIT     5  // Uno Digital Pin 13 (NOTE: D13 can't be pulled-high input due to LED.)
#define SEL_MUX_MASK     ((1<<SEL_MUX0_BIT)|(1<<SEL_MUX1_BIT))
// Multiplexed values
#define SEL_MUX_AXIS_Z0  0
#define SEL_MUX_AXIS_Z1  1
#define SEL_MUX_AXIS_Z2  2
#define SEL_MUX_AXIS_ALL 3

// Define endstop output port
// NOTE: Uno analog pins 4 and 5 are reserved for an i2c interface, and may be installed at
// a later date if flash and memory space allows.
#define EXT_ENDSTOP_DDR   DDRC
#define EXT_ENDSTOP_PORT  PORTC
#define EXT_ENDSTOP_BIT   3  // Uno Analog Pin 3

// Define user-control controls (cycle start, reset, feed hold) input pins.
// NOTE: All CONTROLs pins must be on the same port and not on a port with other input pins (limits).
#define CONTROL_DDR       DDRC
#define CONTROL_PIN       PINC
#define CONTROL_PORT      PORTC

#define RESET_BIT         0 // Uno Analog Pin 0
#define EXT_DIR_BIT		  1 // Uno Analog Pin 1 (was FEED_HOLD_BIT aka "Hold" on the CNC shield)
#define EXT_STEP_BIT	  2 // Uno Analog Pin 2 (was CYCLE_START_BIT aka "Resume" on the CNC shield)

#define CONTROL_INT       PCIE1  // Pin change interrupt enable pin
#define CONTROL_INT_vect  PCINT1_vect
#define CONTROL_PCMSK     PCMSK1 // Pin change interrupt register
#define CONTROL_MASK ((1<<RESET_BIT)|(1<<EXT_STEP_BIT)|(1<<EXT_DIR_BIT))
#define CONTROL_INVERT_MASK CONTROL_MASK // May be re-defined to only invert certain control pins.
  
// Define probe switch input pin.
#define PROBE_DDR       DDRC
#define PROBE_PIN       PINC
#define PROBE_PORT      PORTC
#define PROBE_BIT       5  // Uno Analog Pin 5
#define PROBE_MASK      (1<<PROBE_BIT)
