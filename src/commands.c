/*
 * commands.c
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#include "main.h"
#include "commands.h"
#include "serial.h"

void command_execute(const char* cmd)
{
	print_pstr("Command:");
	print_string(cmd);
	print_pstr("\n");

	// ack by reversing the direction (temporary debug)
	DIRECTION_PORT = DIRECTION_PORT  ^ (1<<X_DIRECTION_BIT);
	DIRECTION_PORT = DIRECTION_PORT  ^ (1<<Y_DIRECTION_BIT);
	DIRECTION_PORT = DIRECTION_PORT  ^ (1<<Z_DIRECTION_BIT);

}

void command_collect()
{
	static char cmd_buf[32];
	static uint8_t cmd_len= 0;
	uint8_t c= serial_read();
	if(c!=SERIAL_NO_DATA)
	{
		serial_write(c);
		if(c=='\r'||c=='\n')
		{
			if(cmd_len==0xFF)
				print_pstr("N:discarded\n");
			else
			{
				cmd_buf[cmd_len]= 0;
				command_execute(cmd_buf);
			}
			cmd_len= 0;
		}
		else if(c<' ')
			cmd_len= 0xFF; // ignore command when a weird character is received
		else if(cmd_len>=sizeof(cmd_buf)-1)
		{
			print_pstr("N:too long\n");
			cmd_len= 0;
		}
		else
			cmd_buf[cmd_len++]= c;
	}
}

