/*
 * commands.c
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#include "main.h"
#include "commands.h"
#include "serial.h"
#include "limits.h"
#include "serial.h"
#include "steppers.h"

#define HOME_SEEK_PREPARE_MM	8.0		// max expected bed skew before homing (will go down by this number)
#define HOME_SEEK_UP_MM			400.0	// bed may start completely at the bottom
#define HOME_SEEK_COARSE_RATIO	0.4		// how fast to seek (first pass only)
#define LEVEL_SEEK_UP_MM		(2*HOME_SEEK_PREPARE_MM)

// Temporary modes: make sure to check your scope for these automatic status/instances!
#define TEMP_RELATIVE_MODE		Backup<bool> sam(steppers_relative_mode, true)
#define TEMP_IGNORE_LIMITS 		Backup<volatile bool> sre(steppers_respect_endstop,false);


static char cmd_buf[32];
static uint8_t cmd_len= 0;
static float speed_factor= 1.0;

void cmd_show_status();

bool error(const char* cmd)
{
	print_char('?');
	print_string(cmd);
	print_char('\n');
	delay_ms(10);
	return false;
}

void info(const char* cmd)
{
	print_char(';');
	print_string(cmd);
	print_char('\n');
	delay_ms(10);
}

bool success(const char* cmd)
{
	print_char('$');
	print_string(cmd);
	print_char('\n');
	delay_ms(10);
	return true;
}

// ======================= helpers =======================

bool detect_up(float speed)
{
	TEMP_RELATIVE_MODE;
	// Up by 20 (expecting to hit the limits, head is in the center of the bed, bed is approximately flat -- as always!)
	sticky_limits= 0;
	move_modal(-HOME_SEEK_UP_MM, speed); // slow upwards (we're expecting to trigger one or many end stops, so check sticky_limits!)
	delay_ms(10);
	return(sticky_limits!=0);
}

bool detect_up_axis(uint8_t axis, float speed)
{
	TEMP_RELATIVE_MODE;
	// Up by 20 (expecting to hit the limits, head is in the center of the bed, bed is approximately flat -- as always!)
	sticky_limits= 0;
	move_modal_axis(axis, -LEVEL_SEEK_UP_MM, speed); // slow upwards. We're expecting to trigger at least one end stop
	delay_ms(10);
	return(sticky_limits!=0);
}

bool down_detach()
{
	// TODO: change movement to be short sequences of [down/up/down] to avoid sensor saturation
	TEMP_IGNORE_LIMITS; // else no movement will be done
	TEMP_RELATIVE_MODE;
	uint8_t trials= 0;
	while(++trials<10)
	{
		sticky_limits= 0;
		bool rt;
		stepper_set_targets(1, 0.2); // asynchronous call: down slowly, just to detach the bed from the tool head
		while((rt=limits_get_rt_states()) && steppers_are_moving() && !nmi_reset);
		stepper_set_targets(0,1); // stop all movement asap (we are in relative mode)
		if(!rt) break;
		delay_ms(10);
	}
	if(trials>=5) return false;
	sticky_limits= 0;
	return true;
}

bool down_detach_single(uint8_t axis)
{
	// TODO: change movement to be short sequences of [down/up/down] to avoid sensor saturation
	TEMP_IGNORE_LIMITS; // else no movement will be done
	TEMP_RELATIVE_MODE;
	uint8_t trials= 0;
	while(++trials<10)
	{
		sticky_limits= 0;
		bool rt;
		stepper_set_target(axis, 1, 0.2); // asynchronous call: down slowly, just to detach the bed from the tool head
		while((rt=limits_get_rt_states()) && stepper_is_moving(axis) && !nmi_reset);
		stepper_set_target(axis, 0,1); // stop all movement asap (we are in relative mode)
		if(!rt) break;
		delay_ms(10);
	}
	if(trials>=5) return false;
	sticky_limits &= ~(1<<axis);
	return true;
}

// ======================= All 3 axis =======================

void cmd_show_status()
{
	uint8_t rl= limits_get_rt_states(); // get a copy as fast as possible as it is volatile!
	uint8_t sl= sticky_limits;

	if(stepper_are_powered())
		info("offline");
	else
		info("online");

	if(limits_are_enforced())
		info("limited");
	else
		info("unlimited");

	print_pstr(";limits=");
	print_unsigned_int8(sl,2,3);
	print_pstr("\n");

	print_pstr(";rt_limits=");
	print_unsigned_int8(rl,2,3);
	print_pstr("\n");

	for(uint8_t axis=0; axis<3; ++axis)
	{
		print_pstr(";axis_");
		print_char('X'+axis);
		print_char('=');
		print_float(stepper_get_position(axis));
		print_pstr("\n");
	}
}

bool cmd_home_center()
{
	TEMP_RELATIVE_MODE;
	limits_enable();

	// Pre-down all bed
	info("h/down");
	{
		TEMP_IGNORE_LIMITS;
		sticky_limits= 0;
		move_modal(HOME_SEEK_PREPARE_MM, 1);
	}

	// Coarse upwards (first home seek at medium speed)
	{
		info("h/coarse");
		if(!detect_up(HOME_SEEK_COARSE_RATIO))
		{
			nmi_reset= true; // hard failure: homing is vital
			return error("hn/upwards");
		}
		set_origin();
		delay_ms(100);
	}

	// Slightly down again
	{
		TEMP_IGNORE_LIMITS;
		move_modal(1.0, 0.5);
		delay_ms(400); // add enough time for the sensors to forget the last pressure level
	}

	// Fine upwards and set origin again
	{
		info("h/fine");
		detect_up(0.1);
		set_origin();
	}

	// Down until sensors detect nothing anymore
	{
		info("h/back");
		if(!down_detach())
		{
			nmi_reset= true; // hard failure: homing is vital
			error("h/unstick");
		}
		info("h/origin");
		// cmd_show_status(); // debug
		set_origin();
		delay_ms(100);
		sticky_limits= 0; // sometimes the bed is slightly elastic
	}
	return true;
}

// ======================= One axis only =======================


bool cmd_home_axis(uint8_t axis)
{
	TEMP_RELATIVE_MODE;
	limits_enable();

	// Pre-down all bed as it may be skewed
	info("hn/down");
	{
		TEMP_IGNORE_LIMITS;
		sticky_limits= 0;
		//move_modal(HOME_SEEK_PREPARE_MM, 1); // all three Z axes?
		move_modal_axis(axis,HOME_SEEK_PREPARE_MM,1);
	}

	// Coarse upwards (first home seek at medium speed)
	{
		info("hn/coarse");
		if(!detect_up_axis(axis, 0.25))
		{
			nmi_reset= true; // hard failure: homing is vital
			return error("hn/upwards");
		}
		set_origin_single(axis);
		delay_ms(100);
	}

	// Slightly down again
	{
		TEMP_IGNORE_LIMITS;
		move_modal_axis(axis, 1.0, 0.5);
		delay_ms(400); // add enough time for the sensors to forget the last pressure level
	}

	// Fine upwards and set origin again
	{
		info("hn/fine");
		detect_up_axis(axis, 0.1);
		set_origin_single(axis);
	}

	// Down until sensors detect nothing anymore
	{
		info("hn/back");
		if(!down_detach_single(axis))
		{
			nmi_reset= true; // hard failure: homing is vital
			error("h/unstick");
		}
		info("hn/origin");
		// cmd_show_status(); // debug
		set_origin_single(axis);
		delay_ms(100);
		sticky_limits &= ~(1<<(axis+1)); // sometimes the bed is slightly elastic
	}

	// success
	return true;
}


// ======================= Command interpreter =======================

bool command_collect()
{
	uint8_t c= serial_read();
	if(c!=SERIAL_NO_DATA)
	{
		serial_write(c);
		if(c=='\r'||c=='\n')
		{
			if(cmd_len==0xFF)
				error("discarded");
			else
			{
				cmd_buf[cmd_len]= 0;
				cmd_len= 0;
				return true;
			}
			cmd_len= 0;
		}
		else if(c<' ')
			cmd_len= 0xFF; // ignore command when a weird character is received
		else if(cmd_len>=sizeof(cmd_buf)-1)
		{
			error("too long\n");
			cmd_len= 0;
		}
		else
			cmd_buf[cmd_len++]= c;
	}
	return false;
}


const char* string_to_float(const char* p, float* v)
{
	float value= 0;
	while(*p>='0' && *p<='9')
		value= (value*10) + (*(p++)-'0');
	if(*p=='.')
	{
		float k= 0.1;
		while(*p>='0' && *p<='9')
		{
			value+= (*(p++)-'0')*k;
			k/= 10;
		}
	}
	*v= value;
	return p;
}

bool enabled()
{
	if(!stepper_are_powered())
	{
		info("disabled");
		return false;
	}
	return true;
}

bool run(const char* cmd/*= NULL*/)
{
	char cmd0= cmd[0];
	char cmd1= cmd[1];

	if(cmd0=='?')
	{
		print_pstr_slow(";help:\n\
;! - status\n\
;p<0|1> - power\n\
;s<ratio> - speed\n\
;m<R|A> - relative/absolute mode\n\
;l<0|1> - respect limits\n\
;o - off limits\n\
;h - main home\n\
;z - zero origin\n\
;c<0-2> - calibrate\n\
;g<height> - move bed\n");
		return true;
	}

	// ----------------------------------------------------------------------------------------
	if(cmd0=='!') // ! - show status
	{
		if(cmd1) return false;
		cmd_show_status();
		return true;
	}

	// ---------------------------------------------------------------------------------------- config

	if(cmd0=='p') // p<0|1> - stepper power
	{
		if(cmd1 && !cmd[2])
		{
			if(cmd1=='0')	{ stepper_power(false); return true; }
			if(cmd1=='1')	{ stepper_power(true); return true; }
		}
		info("0|1?");
		return false;
	}

	if(cmd0=='s') // s<ratio> - speed ratio
	{
		if(!cmd1) return false;
		float pos;
		const char* p= string_to_float(cmd+1, &pos);
		if(*p)
		{
			info("ratio?");
			return false;
		}
		speed_factor= pos;
		return true;
	}

	if(cmd0=='m') // m<R|A> - relative or absolute mode
	{
		if(cmd1 && !cmd[2])
		{
			if(cmd1=='r')	{ steppers_relative_mode= true; return true; }
			if(cmd1=='a')	{ steppers_relative_mode= false; return true; }
		}
		info("R|A?");
		return false;
	}

	if(cmd0=='l') // l<0|1> - enable/disable limits interruptions
	{
		if(cmd1 && !cmd[2])
		{
			if(cmd1=='0')	{ limits_disable(); return true; }
			if(cmd1=='1')	{ limits_enable(); return true; }
		}
		info("0|1?");
		return false;
	}


	// ---------------------------------------------------------------------------------------- movement: setup

	if(cmd0=='h') // h - homing
	{
		if(cmd1) return false;
		stepper_power(true); // one way to boot up the steppers
		return cmd_home_center();
	}

	if(cmd0=='c') // c<0-2> - calibrate
	{
		uint8_t axis= (cmd1-'0');
		if(axis>2)
		{
			info("0-N?");
			return false;
		}

		stepper_power(true); // one way to boot up the steppers
		return cmd_home_axis(axis);
	}

	if(cmd0=='o') // x - get down, out of the limits
	{
		if(cmd1) return false;
		if(!enabled()) return false;
		return(down_detach());
	}

	// ---------------------------------------------------------------------------------------- movement: bed height

	if(cmd0=='z') // z - zero the origin here
	{
		if(cmd1) return false;
		set_origin();
		return true;
	}

	if(cmd0=='g') // G<mm> : move to a position
	{
		if(!enabled()) return false;
		float pos;
		const char* p= string_to_float(cmd+1, &pos);
		if(*p)
		{
			info("height?");
			return false;
		}

		if(move_modal(pos, speed_factor))
			return true;

		info("limit_hit");
		steppers_zero_speed(); // we will restart at slow speed if "l0" is send (i.e. do not enforce limits)
		return false;
	}

	// ----------------------------------------------------------------------------------------

	info("unknown");

	return false;
}

// ---------------------------------------------------------------------------------

void command_execute(const char* cmd/*= NULL*/)
{
	if(!cmd)
		cmd= cmd_buf;

	// Echo input command
	print_pstr(">");
	print_string(cmd);
	print_pstr("\n");

	if(run(cmd))
		success(cmd);
	else
		error(cmd);
}

