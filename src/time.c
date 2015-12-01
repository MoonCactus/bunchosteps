/*
 * time.cpp
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */


#include "main.h"

/* some vars */
uint64_t _millis = 0;
uint16_t _1000us = 0;
/* CSn2 CSn1 CSn0 Description
   ---- ---- ---- -----------
   0    0    0  No Clock Source
   0    0    1  No prescaling
   0    1    0  CLKio/8
   0    1    1  CLKio/64
   1    0    0  CLKio/256
   1    0    1  CLKio/1024
   1    1    0  Falling Edge of T0 (external)
   1    1    1  Rising Edge of T0 (external)
 */
#define TIMER_PRESCALER 8
// Count: Timer0 counts up to 0xFF and then signals overflow
#define TIMER_TICKS (F_CPU/TIMER_PRESCALER/1000)
//#if (TIMER_TICKS > 0xFF)
//#pragma error "timer ticks out of bounds"
//#endif
#define TIMER_COUNT (0xFF-TIMER_TICKS)


void millis_init()
{
	uint8_t sreg = SREG;
	cli();

	TCCR1A = 0;
	// set timer0 with CLKio/8 prescaler
	TCCR0B = _BV(CS01) | _BV(CS00);
	// clear any TOV1 Flag set when the timer overflowed
	TIFR0 &= ~TOV0;
	// set timer0 counter initial value to 0
	TCNT0 = 0x0;
	// enable timer overflow interrupt for Timer0
	TIMSK0 = _BV(TOIE0);
	// clear the Power Reduction Timer/Counter0
	PRR &= ~PRTIM0;

	SREG = sreg;
}

// timer0 interrupts routines
ISR(TIMER0_OVF_vect)
{
	// Set the counter for the next interrupt
	TCNT0 = (uint8_t)TIMER_COUNT;
	// Overflow Flag is automatically cleared
	_millis++;
}

// Get elapsed time in milliseconds
uint64_t millis()
{
  uint64_t m;
  cli();
  m= _millis;
  sei();
  return m;
}


//===============
