/*
 * Module for receiving and decoding of wireless weather station
 * sensor data (433MHz). Protocol used by Cresta/Irox/Mebus/Nexus/
 * Honeywell/Hideki/TFA weather stations.
 * 
 * Protocol was reverse engineered by Ruud v Gessel
,* and documented in "Cresta weather sensor protocol", see
 * http://members.upc.nl/m.beukelaar/Crestaprotocol.pdf
 *
 * This module utilizes code of the Arduino
 * decoder library "433MHzForArduino" for decoding the sensor data,
 * see https://bitbucket.org/fuzzillogic/433mhzforarduino
 *
 * License: GPLv3. See license.txt
 */

#ifndef _CRESTA_INTERRUPTHANDLER_H_
#define _CRESTA_INTERRUPTHANDLER_H_

#include <linux/workqueue.h>
#include <linux/string.h>

#define DRIVER_AUTHOR "Sebastian Meier <sebastian.alexander.meier@gmail.com>"
#define DRIVER_DESC   "Cresta Sensor Driver"
#define CRESTA_KFIFO_SIZE       4096		//number of bytes (elements?) we want to store in FIFO must pe power of 2

 
// we receive interrupts on GPIO 27 (pin 13 on raspberry pi b+)
#define CRESTA_GPIO                27

 
// human readable description, i.e. for 'cat /proc/interrupt' description
#define CRESTA_GPIO_DESC           "Cresta 433MHz receiver"
#define CRESTA_GPIO_DEVICE_DESC    "cresta_receiver"


struct cresta_work {
    struct work_struct ws;
};

void cresta_manchester_decoder (uint32_t);
void reset_manchester_decoder(uint32_t);


#endif
