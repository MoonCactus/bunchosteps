/*
 * main.h
 *
 *  Created on: Nov 30, 2015
 *      Author: jeremie
 */

#ifndef MAIN_H_
#define MAIN_H_

// Define standard libraries used by Grbl.
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Define the Grbl system include files. NOTE: Do not alter organization.
#include "config.h"

#ifdef CPU_MAP_ATMEGA328P // (Arduino Uno) Officially supported by Grbl.
  #include "cpu_map/cpu_map_atmega328p.h"
#endif
#ifdef CPU_MAP_ATMEGA2560 // (Arduino Mega 2560) Working @EliteEng
  #include "cpu_map/cpu_map_atmega2560.h"
#endif

#include "nuts_bolts.h"
#include "time.h"
#include "limits.h"
#include "serial.h"
#include "steppers.h"
#include "commands.h"

#endif /* MAIN_H_ */
