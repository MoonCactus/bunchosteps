/*
 * commands.h
 *
 *  Created on: Dec 1, 2015
 *      Author: jeremie
 */

#ifndef COMMANDS_H_
#define COMMANDS_H_

bool command_collect();
void command_execute(const char* cmd= NULL);
void load_axes_offsets();
void save_axes_offsets();

#endif /* COMMANDS_H_ */
