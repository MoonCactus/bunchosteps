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

#define EPSILON_POS				0.01	// calibration is considered OK when diff is below with value

#define HOME_SEEK_UP_MM			400.0	// bed may start completely at the bottom
#define SEEK_DOWN_RATIO			0.8		// we don't want to trigger any sensor
#define SEEK_DOWN_SETTLE_MS		400		// time to wait before seeking up again slowly
#define HOME_SEEK_COARSE_RATIO	0.4		// how fast to seek (first pass)
#define HOME_SEEK_FINE_RATIO	0.1		// how fast to seek (second pass)

#define CALIBRATION_PRE_SEEK_MM	1.0		// retract after coarse look up
#define CALIBRATION_SEEK_UP_MM	2.0		// max travel up when looking for an axis home

// Temporary modes: make sure to check your scope for these automatic status/instances!
#define TEMP_RELATIVE_MODE		Backup<bool> sam(steppers_relative_mode, true)
#define TEMP_ABSOLUTE_MODE		Backup<bool> sam(steppers_relative_mode, false)
#define TEMP_IGNORE_LIMITS 		Backup<volatile bool> sre(steppers_respect_endstop,false);

/*extern*/ uint8_t external_mode= 0;

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

void info(const char* cmd, float v)
{
	print_char(';');
	print_string(cmd);
	print_float(v);
	print_char('\n');
	delay_ms(10);
}

void info_axis(int axis)
{
	print_string(";axis:H");
	print_integer(axis);
	print_char('=');
	print_float( stepper_get_position(axis) );
	print_string(",L");
	print_char(sticky_limit_is_hit(axis)?'1':'0');
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

/**
 * Move bed down until sensors are no more activated.
 * The usual context is to call this *immediately* after homing up and before setting home with z
 */
bool down_detach()
{
	// TODO: change movement to be short sequences of [down/up/down] to avoid sensor saturation
	TEMP_IGNORE_LIMITS; // else no movement will be done
	TEMP_RELATIVE_MODE;
	sticky_limits= 0;
	if(limits_get_rt_states()) stepper_set_targets(5, 0.01); // very slow asynchronous call: just to detach the bed from the tool head
	while(limits_get_rt_states() && steppers_are_moving() && !nmi_reset); // TODO: add length limit!
	steppers_settle_here(); // stop all movement asap
	return !limits_get_rt_states();
}

bool down_detach_single(uint8_t axis)
{
	// TODO: change movement to be short sequences of [down/up/down] to avoid sensor saturation
	TEMP_IGNORE_LIMITS; // else no movement will be done
	TEMP_RELATIVE_MODE;
	sticky_limits &= ~(1<<axis);

	if(limits_get_rt_states()&(1<<axis)) stepper_set_target(axis, 5, 0.01); // asynchronous call: down slowly, just to detach the bed from the tool head
	while((limits_get_rt_states()&(1<<axis)) && stepper_is_moving(axis) && !nmi_reset);
	stepper_settle_here(axis); // stop all movement asap

	return !(limits_get_rt_states() & (1<<axis));
}

// Detect bed upwards and retract a little to detach from the sensor
bool detect_up(float speed)
{
	TEMP_RELATIVE_MODE;
	// Up by 20 (expecting to hit the limits, head is in the center of the bed, bed is approximately flat -- as always!)
	sticky_limits= 0;
	move_modal(-HOME_SEEK_UP_MM, speed); // slow upwards (we're expecting to trigger one or many end stops)
	steppers_settle_here();
	bool ret= (sticky_limits!=0); // positive when sticky_limits is enabled
	down_detach(); delay_ms(100); down_detach();
	return ret;
}

// Detect bed upwards and retract a little to detach from the sensor
bool detect_up_axis(uint8_t axis, float speed)
{
	TEMP_RELATIVE_MODE;
	// Up by 20 (expecting to hit the limits, head is in the center of the bed, bed is approximately flat -- as always!)
	sticky_limits= 0;
	move_modal_axis(axis, -CALIBRATION_SEEK_UP_MM, speed); // slow upwards. We're expecting to trigger at least one end stop
	stepper_settle_here(axis);
	bool ret= ((sticky_limits&(1<<axis))!=0); // we want this axis to hit the limit
	down_detach_single(axis); delay_ms(100); down_detach_single(axis);
	return ret;
}

// ======================= All 3 axis =======================

void cmd_show_status()
{
	uint8_t rl= limits_get_rt_states(); // get a copy as fast as possible as it is volatile!
	uint8_t sl= sticky_limits;

	if(stepper_are_powered())
		info("pon");
	else
		info("poff");

	if(external_mode)
		info("ext");
	else
		info("cal");

	if(limits_are_enforced())
		info("lon");
	else
		info("loff");

	print_pstr(";stl=");
	print_unsigned_int8(sl,2,3);
	print_pstr("\n");

	print_pstr(";rtl=");
	print_unsigned_int8(rl,2,3);
	print_pstr("\n");

	for(uint8_t axis=0; axis<3; ++axis)
	{
		print_pstr(";");
		print_char('X'+axis);
		print_char('=');
		print_float(stepper_get_position(axis));
		print_pstr("\n");
	}

	print_pstr(";ram=");
	print_integer(get_free_memory());
	print_char('\n');

}

/**
 * 3-way simultaneous homing and origin ("standard homing", may occur after calibration)
 */
bool cmd_home_center()
{
	TEMP_RELATIVE_MODE;
	limits_enable();

	// Coarse upwards (first home seek at medium speed)
	{
		info("h/coarse");
		if(!detect_up(HOME_SEEK_COARSE_RATIO)) goto failToDetectUpwards;
	}

	// Down by a little bit again to redo a finer seek
	{
		TEMP_IGNORE_LIMITS;
		move_modal(CALIBRATION_PRE_SEEK_MM, SEEK_DOWN_RATIO);
		delay_ms(SEEK_DOWN_SETTLE_MS); // TODO: we should be able to reset the sensor module (so as to forget the pressure event)
	}

	// Upwards again, but slower
	{
		info("h/fine");
		if(!detect_up(HOME_SEEK_FINE_RATIO)) goto failToDetectUpwards;
	}

	// Finalize
	info("h/origin");
	set_origin();
	delay_ms(50);
	sticky_limits= 0; // sometimes the bed is slightly elastic
	return true;

failToDetectUpwards:
	nmi_reset= true; // hard failure: homing is vital
	return false;
}

// ======================= One axis only =======================


bool cmd_calibrate_axis(uint8_t axis)
{
	TEMP_RELATIVE_MODE;
	limits_enable();

	if(axis==0)
		return cmd_home_center(); // axis zero is the reference, so calibrating axis zero is equivalent to homing + set origin

	// Grouped coarse upwards (aka homing without setting origins)
	{
		info("cn/common");
		if(!detect_up(HOME_SEEK_COARSE_RATIO)) goto failure;
		delay_ms(100);
	}

	// Lower the individual axis slightly
	{
		TEMP_IGNORE_LIMITS;
		move_modal_axis(axis, CALIBRATION_PRE_SEEK_MM, SEEK_DOWN_RATIO);
		delay_ms(SEEK_DOWN_SETTLE_MS); // add enough time for the sensors to forget the last pressure level
	}

	// Seek end stop upwards for this axis
	{
		info("cn/fine");
		if(!detect_up_axis(axis, HOME_SEEK_FINE_RATIO)) goto failure;
	}

	// Numerically, we say we are back at the same height as the reference axis, i.e. zero since we homed in the first place
	{
		info("cn/offset");
		stepper_zero(axis);
	}

	// success
	return true;

failure:
	nmi_reset= true; // hard failure: calibration is vital
	return false;
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
	bool negate= false;
	if(*p=='-') { negate=true; ++p; }
	while(*p>='0' && *p<='9')
		value= (value*10) + (*(p++)-'0');
	if(*p=='.')
	{
		++p;
		float k= 0.1;
		while(*p>='0' && *p<='9')
		{
			value+= (*(p++)-'0')*k;
			k/= 10;
		}
	}
	if(negate) value= -value;
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
;=<C|E> - config vs. external mode\n\
;s<0-2> - settle here\n\
;p<0|1> - power\n\
;s<ratio> - speed ratio\n\
;m<R|A> - relative/absolute\n\
;x<0-2> - clear limits\n\
;l<0|1> - respect limits\n\
;d<0-2> - detach from limits\n\
;h - main home\n\
;z<0-2> - zero origin\n\
;c<0-2> - calibrate\n\
;o<0-2,mm> - override position\n\
;g<mm> - move bed\n");
		return true;
	}

	// ----------------------------------------------------------------------------------------
	if(cmd0=='!') // ! - show status
	{
		if(cmd1) return false;
		cmd_show_status();
		return true;
	}

	if(cmd0=='s') // s -settle here (stop movement)
	{
		if(!cmd1)
		{
			steppers_settle_here();
			return true;
		}
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		stepper_settle_here(axis);
		return true;
	}

	// ---------------------------------------------------------------------------------------- config

	if(cmd0=='=') // =<C|E> - configuration/external drive. Transparent just transmits dir/steps from input pins.
	{
		if(cmd1 && !cmd[2])
		{
			if(cmd1=='C')	{ external_mode= 0; return true; }
			if(cmd1=='E')	{ stepper_power(true); external_mode= 1; return true; }
		}
		info("C|E?");
		return false;
	}

	if(cmd0=='p') // p<0|1> - stepper power
	{
		if(cmd1 && !cmd[2])
		{
			if(cmd1=='0')	{ stepper_power(false); return true; }
			if(cmd1=='1')	{ stepper_power(true); return true; }
		}
		goto badAxis;
	}

	if(cmd0=='r') // r<ratio> - speed ratio
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
			if(cmd1=='R')	{ steppers_relative_mode= true; return true; }
			if(cmd1=='A')	{ steppers_relative_mode= false; return true; }
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
		goto badAxis;
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
		if(axis>2) goto badAxis;
		stepper_power(true); // one way to boot up the steppers
		return cmd_calibrate_axis(axis);
	}

	if(cmd0=='x') // x or x<0-2> - clear sticky limits and force/resume movement if any (dangerous)
	{
		steppers_zero_speed(); // make sure steppers restart at low speed
		if(!cmd1)
		{
			sticky_limits= 0;
			return true;
		}
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		sticky_limits &= ~(1<<axis);
		return true;
	}

	if(cmd0=='d') // d or d<0-2> - detach from limits by moving down
	{
		if(!enabled()) return false;
		if(!cmd1)
			return(down_detach());
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		return down_detach_single(axis);

	}

	// ---------------------------------------------------------------------------------------- movement: bed height

	if(cmd0=='z') // z or z<0-2> - zero the origin here
	{
		if(!cmd1)
		{
			set_origin();
			return true;
		}
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		set_origin_single(axis);
		return true;
	}

	if(cmd0=='g') // g<mm> : move to a position
	{
		if(!enabled()) return false;
		float pos;
		const char* p= string_to_float(cmd+1, &pos);
		if(*p) goto badHeight;

		if(move_modal(pos, speed_factor))
			return true;

		info("limit_hit");
		steppers_zero_speed(); // we will restart at slow speed if "l0" is send (i.e. do not enforce limits)
		return false;
	}

	if(cmd0=='o') // o<0-2=mm> : override axis position
	{
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		cmd+=2;
		while(*cmd && *cmd!='-' && *cmd!='.' && (*cmd<'0' || *cmd>'9')) ++cmd;
		float new_position;
		const char* p= string_to_float(cmd, &new_position);
		if(*p) goto badHeight;
		stepper_override_position(axis, new_position);
		info_axis(axis);
		return true;
	}

	// ----------------------------------------------------------------------------------------

	info("unknown");
	return false;

badAxis:
	info("0-N?");
	return false;

badHeight:
	info("height?");
	return false;
}

// ---------------------------------------------------------------------------------

void command_execute(const char* cmd/*= NULL*/)
{
	if(!cmd)
		cmd= cmd_buf;
	if(!*cmd || *cmd=='\n' || *cmd=='\r')
		return; // keep quiet on these

	// Echo input command
	print_pstr(">");
	print_string(cmd);
	print_pstr("\n");

	if(run(cmd))
		success(cmd);
	else
		error(cmd);
}

