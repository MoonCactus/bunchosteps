/*
 * limits.c
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#include "main.h"
#include "limits.h"

volatile uint8_t limits_sticky_states= 0;

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
}

void limits_init()
{
	DDRB  &= ~0b00001110; // Set as input pins
	PORTB |=  0b00001110;  // Enable internal pull-up resistors. Normal high operation.
	limits_enable();
}

uint8_t limits_get_current_state()
{
  return (PINB & 0b00001110);
}

bool limit_is_hit(int axis)
{
	return (limits_sticky_states & (1<<(axis+1)));
}


ISR(PCINT0_vect) // DEFAULT: Limit pin change interrupt process.
{
	limits_sticky_states|= limits_get_current_state();
}
