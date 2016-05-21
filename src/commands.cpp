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
#include "external.h"
#include <avr/eeprom.h>

#define SEEK_COARSE_SPEED_RATIO		0.3		// how fast to seek (first pass)
#define SEEK_COARSE_LENGTH_MM		400.0	// bed may start completely at the bottom

#define SEEK_FINE_SPEED_RATIO		0.1		// how fast to seek (second pass)
#define SEEK_LENGTH_MM				1.5		// length to look up again after initial hit and retract

#define SEEK_DOWN_RATIO				1		// how fast to retract (limits are ignored anyway)

#define SEEK_DOWN_SETTLE_HOME_MS	200		// time to wait in low state before homing (make sure the end stops are off)
#define SEEK_DOWN_SETTLE_LONG_MS	400		// initial time to wait before seeking up first time
#define SEEK_DOWN_SETTLE_SHORT_MS	150		// time to wait before seeking up again slowly

// Temporary modes: make sure to check your scope for these automatic status/instances!
#define TEMP_RELATIVE_MODE			Backup<bool> _trm(steppers_relative_mode, true)
#define TEMP_ABSOLUTE_MODE			Backup<bool> _tam(steppers_relative_mode, false)
#define TEMP_USE_LIMITS 			Backup<volatile bool> _tul(steppers_respect_endstop,true);
#define TEMP_IGNORE_LIMITS 			Backup<volatile bool> _til(steppers_respect_endstop,false);

#define EEPROM_AXES_OFFSETS_ADDR	64

static char cmd_buf[32];
static uint8_t cmd_len= 0;
static float speed_factor= 1.0;

float axis_offsets[3];

void cmd_show_status();

void load_axes_offsets()
{
	const float *a= (const float *)EEPROM_AXES_OFFSETS_ADDR;
	axis_offsets[0]= eeprom_read_float(a++);
	axis_offsets[1]= eeprom_read_float(a++);
	axis_offsets[2]= eeprom_read_float(a);
}

void save_axes_offsets()
{
	float *a= (float *)EEPROM_AXES_OFFSETS_ADDR;
	eeprom_write_float(a++, axis_offsets[0]);
	eeprom_write_float(a++, axis_offsets[1]);
	eeprom_write_float(a++, axis_offsets[2]);
}

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
	print_string(",o");
	print_float(axis_offsets[axis]);
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

bool force_movement(char axis_letter=0)
{
	steppers_zero_speed(); // make sure steppers restart at low speed
	if(!axis_letter)
	{
		sticky_limits= 0;
		return true;
	}
	uint8_t axis= (axis_letter-'0');
	if(axis>2) return false;
	sticky_limits &= ~(1<<axis);
	return true;
}

/**
 * Move bed down until sensors are no more activated.
 * The usual context is to call this *immediately* after homing up and before setting home with z
 */
bool down_detach()
{
	TEMP_RELATIVE_MODE;
	TEMP_IGNORE_LIMITS; // else no movement may be done ("sticky_limits"
	for(uint8_t pass=0;pass<3;++pass)
	{
		sticky_limits= 0; delay_ms(100); // this is to catch any bouncing limits
		if(limits_get_rt_states() || sticky_limits)
		{
			stepper_set_targets(5, 0.01); // very slow asynchronous call: just to detach the bed from the tool head
			while(limits_get_rt_states() && steppers_are_moving() && !nmi_reset);
	steppers_settle_here(); // stop all movement asap
		}
	}

	sticky_limits= limits_get_rt_states();
	return(!sticky_limits);
}

bool down_detach_single(uint8_t axis)
{
	// TODO: change movement to be short sequences of [down/up/down] to avoid sensor saturation
	TEMP_IGNORE_LIMITS; // else no movement will be done
	TEMP_RELATIVE_MODE;
	while( (limits_get_rt_states() & (1<<axis)) )
	{
		// clear the limit
	sticky_limits &= ~(1<<axis);

		stepper_set_target(axis, 1, 0.01); // asynchronous call: down slowly, just to detach the bed from the tool head
	while((limits_get_rt_states()&(1<<axis)) && stepper_is_moving(axis) && !nmi_reset);
	stepper_settle_here(axis); // stop all movement asap
		delay_ms(100);
	}

	bool r=!(limits_get_rt_states() & (1<<axis));
	return r;
}

// Detect all bed upwards and retract a little to detach from the sensor
bool detect_up(float speed, float length_upwards)
{
	TEMP_RELATIVE_MODE;
	// Up by 20 (expecting to hit the limits, head is in the center of the bed, bed is approximately flat -- as always!)
	sticky_limits= 0;
	move_modal(-length_upwards, speed); // slow upwards (we're expecting to trigger one or many end stops)
	steppers_settle_here();
	bool ret= (sticky_limits!=0); // positive when sticky_limits is enabled
	down_detach();
	return ret;
}

// Detect bed upwards and retract a little to detach from the sensor
bool detect_up_axis(uint8_t axis, float speed, float length_upwards)
{
	TEMP_RELATIVE_MODE;
	TEMP_USE_LIMITS;
	// Up by 20 (expecting to hit the limits, head is in the center of the bed, bed is approximately flat -- as always!)
	sticky_limits= 0;
	move_modal_axis(axis, -length_upwards, speed); // slow upwards. We're expecting to trigger at least one end stop
	stepper_settle_here(axis);
	bool ret= ((sticky_limits&(1<<axis))!=0); // we want this axis to hit the limit
	down_detach_single(axis);
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

	if(steppers_relative_mode)
		info("rel");
	else
		info("abs");

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
		info_axis(axis);

	// print_pstr(";ram="); print_integer(get_free_memory()); print_char('\n');

}

/**
 * 3-way simultaneous homing and origin ("standard homing", may occur after calibration)
 */
bool cmd_home_origin(bool slow_but_safe)
{
	TEMP_RELATIVE_MODE;
	limits_enable();

	if(slow_but_safe)
	{
		TEMP_IGNORE_LIMITS;
		move_modal(SEEK_LENGTH_MM, SEEK_COARSE_SPEED_RATIO);
		delay_ms(SEEK_DOWN_SETTLE_HOME_MS); // dynamic FSR sensors stabilization (idle)
	}

	// Long, coarse upwards seek (first home seek)
	info("h/coarse");
	if(!detect_up(SEEK_COARSE_SPEED_RATIO, SEEK_COARSE_LENGTH_MM)) goto failToDetectUpwards;

	// Down by a little bit again to redo a finer seek
	{
		TEMP_IGNORE_LIMITS;
		move_modal(SEEK_LENGTH_MM, SEEK_DOWN_RATIO);
		delay_ms(SEEK_DOWN_SETTLE_SHORT_MS);
	}

	// Seek for limit switch upwards again, but slower (fine seek)
	{
		info("h/fine");
		if(!detect_up(SEEK_FINE_SPEED_RATIO, SEEK_LENGTH_MM+0.5)) goto failToDetectUpwards;
	}

	// Finalize
	info("h/origin");

	// Contrary to calibration, we are NOT applying any offsets after homing!
	// Why? The external master may rely directly on the end stop signals,
	// so he will have to deal with overall bed offset himself!

	set_origin();
	delay_ms(10);
	sticky_limits= 0;
	return true;

failToDetectUpwards:
	nmi_reset= true; // hard failure: homing is vital
	return false;
}

// ======================= One axis only =======================


bool cmd_calibrate_axis(uint8_t axis, bool slow_but_safe)
{
	TEMP_RELATIVE_MODE;
	limits_enable();

	if(axis==0) // axis zero is the reference in any case, so calibration is
		return cmd_home_origin(slow_but_safe); // equivalent to homing + set origin

	if(slow_but_safe)
	{
		TEMP_IGNORE_LIMITS;
		move_modal(SEEK_LENGTH_MM, SEEK_DOWN_RATIO);
		delay_ms(SEEK_DOWN_SETTLE_LONG_MS); // stabilize dynamic sensors
	}

	// Grouped coarse upwards (aka homing without setting origins)
	{
		info("cn/common");
		if(!detect_up(SEEK_COARSE_SPEED_RATIO, SEEK_COARSE_LENGTH_MM)) goto failure;
	}


	// Lower the individual axis slightly
	{
		TEMP_IGNORE_LIMITS;
		move_modal_axis(axis, SEEK_LENGTH_MM, SEEK_DOWN_RATIO);
		delay_ms(slow_but_safe ? SEEK_DOWN_SETTLE_LONG_MS : SEEK_DOWN_SETTLE_SHORT_MS); // add enough time for the sensors to forget the last pressure level
	}

	// Fine seek end stop upwards for this axis
	{
		info("cn/fine");
		if(!detect_up_axis(axis, SEEK_FINE_SPEED_RATIO, SEEK_LENGTH_MM+0.5)) // only 0.5 mm overshoot
			goto failure;
	}
	// Eventually, apply axis-specific retraction (stored in EEPROM)
	move_modal_axis(axis, axis_offsets[axis], 1);

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

unsigned long command_start_time= 0;
#define COMMAND_COLLECT_TIMEOUT_MS 100
bool command_collect()
{
	uint8_t c= serial_read();
	if(c==SERIAL_NO_DATA)
	{
		if(command_start_time) // a command is being collected
		{
			uint64_t t= millis();
			if(t > command_start_time+COMMAND_COLLECT_TIMEOUT_MS)
			{
				serial_reset_read_buffer(); // Clear serial read buffer
				command_start_time= 0;
				cmd_len= 0;
				cmd_buf[0]= 0;
				error("timeout");
				return false;
			}

		}

	}
	else // we have serial data
	{
		if(!command_start_time)
			command_start_time= millis();
		serial_write(c);
		if(c=='\r'||c=='\n')
		{
			if(cmd_len==0xFF)
				error("discarded");
			else
			{
				cmd_buf[cmd_len]= 0;
				cmd_len= 0;
				command_start_time= 0;
				return true; // we have collected a complete command
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
;h - main home\n\
;z<0-2> - zero origin\n\
;c<0-2> - calibrate\n\
;o<0-2,mm> - record axis offset\n\
;g<0-2> <mm> - move one axis\n\
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

	if(cmd0=='s') // s -settle here (stop movement, and cancels the ones which were paused due to the end stops)
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

	if(cmd0=='l') // l<0|1> - enable/disable limits interruptions (and clear sticky bit)
	{
		if(cmd1 && !cmd[2])
		{
			if(cmd1=='0')	{ limits_disable(); return true; }
			if(cmd1=='1')	{ limits_enable(); return true; }
		}
		goto badAxis;
	}


	// ---------------------------------------------------------------------------------------- movement: setup

	if(cmd0=='h') // h - homing (safe mode)
	{
		if(cmd1) return false;
		stepper_power(true); // one way to boot up the steppers
		return cmd_home_origin(true);
	}

	if(cmd0=='h') // H - homing (quick mode)
	{
		if(cmd1) return false;
		stepper_power(true);
		return cmd_home_origin(false);
	}

	if(cmd0=='d') // d or d<0-2> - detach or detach axis
	{
		stepper_power(true);
		if(!cmd1)
			return down_detach();
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		return down_detach_single(axis);
	}

	if(cmd0=='c') // c<0-2> - calibrate (safe mode)
	{
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		stepper_power(true);
		return cmd_calibrate_axis(axis, true);
	}

	if(cmd0=='C') // C<0-2> - calibrate (quick mode)
	{
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		stepper_power(true);
		return cmd_calibrate_axis(axis, false);
	}

	if(cmd0=='x') // x or x<0-2> - clear sticky limits and force/resume movement if any (very dangerous)
	{
		if(!force_movement(cmd1))
			goto badAxis;
		return true;
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

	if(cmd0=='g') // g<mm> or g<0-2> <mm>: move to a position
	{
		if(!enabled()) return false;
		const char* p= cmd+1;
		uint8_t axis=255;
		if(*p>='0' && *p<='2' && *(p+1)==' ') // syntax variant g<axis> <mm>
		{
			axis= (*p-'0');
			++p;
			while(*p && *p==' ') ++p;
		}
		float pos;
		p= string_to_float(p, &pos);
		if(*p) goto badHeight;

		if(axis<3)
		{
			info("axis move!");
			if(move_modal_axis(axis, pos, speed_factor))
				return true;
		}
		else
		{
			if(move_modal(pos, speed_factor))
				return true;
		}
		info("limit_hit");
		steppers_zero_speed(); // restart at slow speed if resumed
		return false;
	}

	if(cmd0=='o') // o<0-2=mm> : set an individual axis gap
	{
		uint8_t axis= (cmd1-'0');
		if(axis>2) goto badAxis;
		cmd+=2;
		while(*cmd && *cmd!='-' && *cmd!='.' && (*cmd<'0' || *cmd>'9')) ++cmd;
		float new_gap;
		const char* p= string_to_float(cmd, &new_gap);
		if(*p) goto badHeight;
		// Update the axis offset and save it in the EEPROM
		float previous_gap= axis_offsets[axis];
		axis_offsets[axis]= new_gap;
		save_axes_offsets();

		// Shift the position, but keep the same recorded value
		float lastpos= stepper_get_position(axis);
		float gap_offset= new_gap-previous_gap;
		stepper_override_position(axis, lastpos - gap_offset);
		stepper_set_targets(lastpos,0.5); // will sync the axes
		force_movement();

		// Show this axis state
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

