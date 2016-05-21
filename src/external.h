/*
 * external.h
 *
 *  Created on: Dec 15, 2015
 *      Author: jeremie
 */

#ifndef EXTERNAL_H_
#define EXTERNAL_H_

#ifdef USE_EXT_POLLING
void external_poll_z();
#endif

void external_init();
void set_external_axes(uint8_t axis);

void set_external_stepper_mode(bool state);
uint8_t is_external_stepper_mode();

void set_external_endstop(bool state);

#endif /* EXTERNAL_H_ */
