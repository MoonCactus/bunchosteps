/*
  nuts_bolts.h - Header file for shared definitions, variables, and functions
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

#ifndef nuts_bolts_h
#define nuts_bolts_h

extern volatile bool nmi_reset;

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

// Bit field and masking macros
#define bit(n)      (1 << n)

// #define bits_true_atomic(x,mask)   {uint8_t sreg = SREG; cli(); (x) |= (mask); SREG = sreg;  }
// #define bits_false_atomic(x,mask)  {uint8_t sreg = SREG; cli(); (x) &= ~(mask); SREG = sreg; }
// #define bits_toggle_atomic(x,mask) {uint8_t sreg = SREG; cli(); (x) ^= (mask); SREG = sreg;  }

#define bset(x,n)   x |=  (1<<(n))
#define bclr(x,n)   x &= ~(1<<(n))

#define bisset(x,n) (((x) & (1<<(n))) != 0)
#define bisclr(x,n) (((x) & (1<<(n))) == 0)

// Just create an instance of this class to have temporary states for your variables (check your scope!)
template<class T>
class Backup
{
private:
	T& variable_reference;
	T initial_value;
public:
	Backup(T& variable, T new_value_for_this_scope) : variable_reference(variable)
	{
		initial_value= variable_reference;
		variable_reference= new_value_for_this_scope;
	}
	~Backup()
	{
		variable_reference= initial_value;
	}
};

// Debug tool to get free memory in bytes at the called point. Not used otherwise.
int get_free_memory();

// Read a floating point value from a string. Line points to the input buffer, char_counter 
// is the indexer pointing to the current character of the line, while float_ptr is 
// a pointer to the result variable. Returns true when it succeeds
uint8_t read_float(char *line, uint8_t *char_counter, float *float_ptr);

// Delays variable-defined milliseconds. Compiler compatibility fix for _delay_ms().
void delay_ms(uint16_t ms);

// Delays variable-defined microseconds. Compiler compatibility fix for _delay_us().
void delay_us(uint32_t us);

// Main timer initialisation
void millis_init();
uint64_t millis();

// Computes hypotenuse, avoiding avr-gcc's bloated version and the extra error checking.
float hypot_f(float x, float y);

#endif
