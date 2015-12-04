/*
  serial.c - Low level functions for sending and recieving bytes via the serial port
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
#include "serial.h"

uint8_t serial_rx_buffer[RX_BUFFER_SIZE];
uint8_t serial_rx_buffer_head = 0;
volatile uint8_t serial_rx_buffer_tail = 0;

uint8_t serial_tx_buffer[TX_BUFFER_SIZE];
uint8_t serial_tx_buffer_head = 0;
volatile uint8_t serial_tx_buffer_tail = 0;


void serial_init()
{
	// Set baud rate
	#if BAUD_RATE < 57600
		uint16_t UBRR0_value = ((F_CPU / (8L * BAUD_RATE)) - 1)/2 ;
		UCSR0A &= ~(1 << U2X0); // baud doubler off  - Only needed on Uno XXX
	#else
		uint16_t UBRR0_value = ((F_CPU / (4L * BAUD_RATE)) - 1)/2;
		UCSR0A |= (1 << U2X0);  // baud doubler on for high baud rates, i.e. 115200
	#endif
	UBRR0H = UBRR0_value >> 8;
	UBRR0L = UBRR0_value;

	// enable rx and tx
	UCSR0B = (1<<RXEN0) | (1<<TXEN0);
	
	// enable interrupt on complete reception of a byte
	UCSR0B |= 1<<RXCIE0;
	// defaults to 8-bit, no parity, 1 stop bit
}


// Writes one byte to the TX serial buffer. Called by main program.
// TODO: Check if we can speed this up for writing strings, rather than single bytes.
void serial_write(uint8_t data)
{
	// Calculate next head
	uint8_t next_head = serial_tx_buffer_head + 1;
	if (next_head == sizeof(serial_tx_buffer))
		next_head = 0;

	// Wait until there is space in the buffer
	while (next_head == serial_tx_buffer_tail)
	{
		// TODO: Restructure st_prep_buffer() calls to be executed here during a long print.
		// if (sys_rt_exec_state & EXEC_RESET) { return; } // Only check for abort to avoid an endless loop.
	}

	// Store data and advance head
	serial_tx_buffer[serial_tx_buffer_head] = data;
	serial_tx_buffer_head = next_head;

	// Enable Data Register Empty Interrupt to make sure tx-streaming is running
	UCSR0B |=  (1 << UDRIE0);
}


// Data Register Empty Interrupt handler
ISR(USART_UDRE_vect)
{
  uint8_t tail = serial_tx_buffer_tail; // Temporary serial_tx_buffer_tail (to optimize for volatile)
  
	// Send a byte from the buffer
	UDR0 = serial_tx_buffer[tail];

	// Update tail position
	tail++;
	if (tail == sizeof(serial_tx_buffer))
		tail = 0;

	serial_tx_buffer_tail = tail;

	// Turn off Data Register Empty Interrupt to stop tx-streaming if this concludes the transfer
	if (tail == serial_tx_buffer_head)
		UCSR0B &= ~(1 << UDRIE0);
}


// Fetches the first byte in the serial read buffer. Called by main program.
uint8_t serial_read()
{
	uint8_t tail = serial_rx_buffer_tail; // Temporary serial_rx_buffer_tail (to optimize for volatile)
	if (serial_rx_buffer_head == tail)
		return SERIAL_NO_DATA;

	uint8_t data = serial_rx_buffer[tail];

	tail++;
	if (tail == sizeof(serial_rx_buffer))
		tail = 0;
	serial_rx_buffer_tail = tail;

	return data;
}

extern void stepper_power(bool);

ISR(USART_RX_vect)
{
	uint8_t data = UDR0;
	uint8_t next_head;

	if(data==0x18) // control-X has immediate meaning (reset)
	{
		stepper_power(false);
		nmi_reset= true;
		return;
	}
	if(nmi_reset) return;

	next_head = serial_rx_buffer_head + 1;
	if (next_head == sizeof(serial_rx_buffer))
	  next_head = 0;

	// Write data to buffer unless it is full.
	if (next_head != serial_rx_buffer_tail)
	{
		serial_rx_buffer[serial_rx_buffer_head] = data;
		serial_rx_buffer_head = next_head;

	}
}

void serial_reset_read_buffer() 
{
	serial_rx_buffer_tail = serial_rx_buffer_head;
}


// Returns the number of bytes used in the RX serial buffer.
uint8_t serial_get_rx_buffer_count()
{
  uint8_t rtail = serial_rx_buffer_tail; // Copy to limit multiple calls to volatile
  if (serial_rx_buffer_head >= rtail)
	  return(serial_rx_buffer_head-rtail);
  return (sizeof(serial_rx_buffer) - (rtail-serial_rx_buffer_head));
}


// Returns the number of bytes used in the TX serial buffer.
// NOTE: Not used except for debugging and ensuring no TX bottlenecks.
uint8_t serial_get_tx_buffer_count()
{
	uint8_t ttail = serial_tx_buffer_tail; // Copy to limit multiple calls to volatile
	if (serial_tx_buffer_head >= ttail)
		return(serial_tx_buffer_head-ttail);
	return (sizeof(serial_tx_buffer) - (ttail-serial_tx_buffer_head));
}


// ======================== PRINTING ROUTINES


void print_char(const char s)
{
	serial_write(s);
}


void print_string(const char *s)
{
	while (*s)
		serial_write(*s++);
}


// Print a string stored in PGM-memory
void _print_pstr(const char *s, int slow_us)
{
	char c;
	while ((c = pgm_read_byte_near(s++)))
	{
		serial_write(c);
		if(slow_us) delay_us(slow_us);
	}
}

// Prints an uint8 variable with base and number of desired digits.
void print_unsigned_int8(uint8_t n, uint8_t base, uint8_t digits)
{
	unsigned char buf[digits];
	uint8_t i = 0;

	for (; i < digits; i++)
	{
		uint8_t d= n % base ;
		buf[i]= d<10 ? ('0' + d) : ('a'+d);
		n /= base;
	}

	for (; i > 0; i--)
		serial_write(buf[i - 1]);
}


// Prints an uint8 variable in base 2.
void print_uint8_base2(uint8_t n)
{
	print_unsigned_int8(n,2,8);
}


// Prints an uint8 variable in base 10.
void print_uint8_base10(uint8_t n)
{
	uint8_t digits;
	if (n < 10) digits = 1;
	else if (n < 100) digits = 2;
	else digits = 3;
	print_unsigned_int8(n,10,digits);
}


void print_uint32_base10(uint32_t n)
{
	if (n == 0)
	{
		serial_write('0');
		return;
	}

	unsigned char buf[10];
	uint8_t i = 0;

	while (n > 0)
	{
		buf[i++] = n % 10;
		n /= 10;
	}

	for (; i > 0; i--)
		serial_write('0' + buf[i-1]);
}


void print_integer(long n)
{
	if (n >= 0)
		print_uint32_base10(n);
	else
	{
		serial_write('-');
		print_uint32_base10(-n);
	}
}


// Convert float to string by immediately converting to a long integer, which contains
// more digits than a float. Number of decimal places, which are tracked by a counter,
// may be set by the user. The integer is then efficiently converted to a string.
// NOTE: AVR '%' and '/' integer operations are very efficient. Bitshifting speed-up
// techniques are actually just slightly slower. Found this out the hard way.
void print_float(float n, uint8_t decimal_places)
{
  if (n < 0) {
    serial_write('-');
    n = -n;
  }

  uint8_t decimals = decimal_places;
  while (decimals >= 2) { // Quickly convert values expected to be E0 to E-4.
    n *= 100;
    decimals -= 2;
  }
  if (decimals) { n *= 10; }
  n += 0.5; // Add rounding factor. Ensures carryover through entire value.

  // Generate digits backwards and store in string.
  unsigned char buf[10];
  uint8_t i = 0;
  uint32_t a = (long)n;
  buf[decimal_places] = '.'; // Place decimal point, even if decimal places are zero.
  while(a > 0) {
    if (i == decimal_places) { i++; } // Skip decimal point location
    buf[i++] = (a % 10) + '0'; // Get digit
    a /= 10;
  }
  while (i < decimal_places) {
     buf[i++] = '0'; // Fill in zeros to decimal point for (n < 1)
  }
  if (i == decimal_places) { // Fill in leading zero, if needed.
    i++;
    buf[i++] = '0';
  }

  // Print the generated string.
  for (; i > 0; i--)
    serial_write(buf[i-1]);
}


// Floating value printing handlers for special variables types used in Grbl and are defined
// in the config.h.
//  - CoordValue: Handles all position or coordinate values in inches or mm reporting.
//  - RateValue: Handles feed rate and current velocity in inches or mm reporting.
//  - SettingValue: Handles all floating point settings values (always in mm.)
void print_float(float n)
{
    print_float(n,2);
}

