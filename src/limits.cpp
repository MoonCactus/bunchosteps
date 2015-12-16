/*
 * limits.c
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#include "main.h"
#include "limits.h"
#include "external.h"

volatile uint8_t sticky_limits= 0;

// Enable hard limits.
void limits_enable()
{
	PCMSK0 |= 0b00001110; // Uno digital 9,10,11 / Enable specific pins of the Pin Change Interrupt
	PCICR |= (1 << PCIE0); // Enable Pin Change Interrupt
}

// Disable hard limits.
void limits_disable()
{
	PCMSK0 &= ~0b00001110;  // Disable specific pins of the Pin Change Interrupt
	PCICR  &= ~(1 << PCIE0);  // Disable Pin Change Interrupt
	sticky_limits= 0;
}

void limits_init()
{
	DDRB  &= ~0b00001110; // Set as input pins
	PORTB |=  0b00001110;  // Enable internal pull-up resistors. Normal high operation.
	limits_enable();
}

bool limits_are_enforced()
{
	return (PCICR & (1 << PCIE0));
}

uint8_t limits_get_rt_states()
{
  return (PINB & 0b00001110)>>1;
}

bool sticky_limit_is_hit(int axis)
{
	return (sticky_limits & (1<<axis));
}


ISR(PCINT0_vect) // DEFAULT: Limit pin change interrupt process.
{
	uint8_t s= limits_get_rt_states();
	sticky_limits|= s;
	external_endstop(s!=0); // echo the endstop state to the master
}
