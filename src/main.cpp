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
#include "external.h"

#include <avr/eeprom.h>
// ======================= Main =======================

int main(void)
{
	// Initialize system upon power-up.
	cli();
	serial_init();   // Setup serial baud rate and interrupts
	load_axes_offsets();
	limits_init();
	stepper_init();
	external_init();
	sei(); // Enable timers

	//#define DEBUG_OSCILLO
	#ifdef DEBUG_OSCILLO
	for(;;)
	{
		stepper_set_targets(0, .05); while(steppers_are_moving());
		stepper_set_targets(10, .1); while(steppers_are_moving());
	}
	#endif

	print_pstr(";BOOT\n");
	for(;;)
	{
		serial_reset_read_buffer(); // Clear serial read buffer

		stepper_power(false);
		limits_enable();

		// print_pstr(";ram=");print_integer(get_free_memory());print_char('\n');

		uint32_t c=0;
		while(!nmi_reset)
		{
			if(command_collect())
				command_execute();

			if(++c==0)
			{
				serial_write(';');
				serial_write('\n');
			}
		}

		// Here on fatal/reset: retract the bed a little
		nmi_reset= false;
		steppers_zero(); // clear all stepper movement
		limits_enable();
		external_init();

		// Must be in its own block!
		{
			print_pstr(";RESET\n");
			steppers_relative_mode= false;
			set_origin(); // ... not sure it is a good idea though
			delay_ms(10);
			stepper_power(false);
			delay_ms(10);
			nmi_reset= false;
			serial_reset_read_buffer();
			print_pstr("$RESET\n");
			#ifdef DEFAULTS_TO_EXTERNAL_MODE
				stepper_power(true);
			#endif
		}
	}
	return 0; // Never reached
}
