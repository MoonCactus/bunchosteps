/*
  main.c

http://gammon.com.au/interrupts
 1  Reset
 2  External Interrupt Request 0  (pin D2)          (INT0_vect)
 3  External Interrupt Request 1  (pin D3)          (INT1_vect)
 4  Pin Change Interrupt Request 0 (pins D8 to D13) (PCINT0_vect)
 5  Pin Change Interrupt Request 1 (pins A0 to A5)  (PCINT1_vect)
 6  Pin Change Interrupt Request 2 (pins D0 to D7)  (PCINT2_vect)
 7  Watchdog Time-out Interrupt                     (WDT_vect)
 8  Timer/Counter2 Compare Match A                  (TIMER2_COMPA_vect)
 9  Timer/Counter2 Compare Match B                  (TIMER2_COMPB_vect)
10  Timer/Counter2 Overflow                         (TIMER2_OVF_vect)
11  Timer/Counter1 Capture Event                    (TIMER1_CAPT_vect)
12  Timer/Counter1 Compare Match A                  (TIMER1_COMPA_vect)
13  Timer/Counter1 Compare Match B                  (TIMER1_COMPB_vect)
14  Timer/Counter1 Overflow                         (TIMER1_OVF_vect)
15  Timer/Counter0 Compare Match A                  (TIMER0_COMPA_vect)
16  Timer/Counter0 Compare Match B                  (TIMER0_COMPB_vect)
17  Timer/Counter0 Overflow                         (TIMER0_OVF_vect)
18  SPI Serial Transfer Complete                    (SPI_STC_vect)
19  USART Rx Complete                               (USART_RX_vect)
20  USART, Data Register Empty                      (USART_UDRE_vect)
21  USART, Tx Complete                              (USART_TX_vect)
22  ADC Conversion Complete                         (ADC_vect)
23  EEPROM Ready                                    (EE_READY_vect)
24  Analog Comparator                               (ANALOG_COMP_vect)
25  2-wire Serial Interface  (I2C)                  (TWI_vect)
26  Store Program Memory Ready                      (SPM_READY_vect)

*/

#include <avr/io.h>
#include <stdbool.h>
#include <avr/interrupt.h>

#include "main.h"
#include "serial.h"
#include "limits.h"
#include "steppers.h"
#include "commands.h"

void print_limits()
{
	int i;
	print_pstr("LRT=");
	uint8_t bs= limits_get_current_states()>>1;
	for(i=0; i<3; ++i)
	{
		print_string((bs&1) ? "*" : "-");
		bs>>=1;
	}
	print_pstr("  L=");

	bs= limits_sticky_states>>1;
	for(i=0; i<3; ++i)
	{
		print_string((bs&1) ? "*" : "-");
		bs>>=1;
	}
	print_pstr("\n");
}

void print_tmp_stats()
{
	print_string(" ...pos="); print_integer(stepper_get_position(0));
	print_string(" dir="); print_integer(stepper_get_direction(0));
	print_string("\n"); delay_ms(500);
}

int main(void)
{
	// Initialize system upon power-up.
	cli();
	serial_init();   // Setup serial baud rate and interrupts
	limits_init();
	stepper_init();
	sei(); // Enable timers

	serial_reset_read_buffer(); // Clear serial read buffer
	print_pstr("Y:booted\n");

	print_free_memory();

	stepper_power(false);
	delay_ms(1000);
	stepper_power(true);

	stepper_speed/= 1;

	for(int pass= 0; pass<5; ++pass)
	{
		stepper_set_targets(-20.0);
		while(steppers_are_moving()) if(limits_sticky_states) goto fail;

		stepper_set_targets(0.0);
		while(steppers_are_moving()) if(limits_sticky_states) goto fail;
	}
fail:
	stepper_power(false);
	if(limits_sticky_states)
		print_string("Fatal on limits.");


	/*
	uint64_t m= millis();
	uint64_t cycle_len=3000;
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
*/


	print_string("Done, please reset.");
	for(;;)
	return 0; // Never reached
}
