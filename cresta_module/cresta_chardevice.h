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

#ifndef _CRESTA_CHARDEVICE_H_
#define _CRESTA_CHARDEVICE_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include "../cresta_common/cresta_common.h"

#define CRESTA_MAX_SENSOR_COUNT 255


/*
 * We create a character device per sensor.
 * Sensor count is used when assigning a name
 * to the sensor.
 * Typically a weather station only supports one sensor
 * per "channel" or sensor type.
 * With our approach we're able to support up to
 * 255 sensors (entire address space)
 */ 
struct cresta_sensor_counts {
    uint8_t thermohygro_ch1_count;
    uint8_t thermohygro_ch2_count;
    uint8_t thermohygro_ch3_count;
    uint8_t thermohygro_ch4_count;
    uint8_t thermohygro_ch5_count;
    uint8_t anemometer_count;
    uint8_t uv_count;
    uint8_t rain_count;  
};

/*
 * Internal representation of a sensor.
 * Note: sensors are stored in a simple linked list
 */

struct cresta_dev {
    struct list_head list;
    uint8_t	sensor_addr;
    uint8_t	sensor_type;
    struct cdev cdev; //character device belonging to sensor
    dev_t       dev;
    struct mutex measurement_data_mutex;
    struct cresta_measurement_data* current_data;
};


int cresta_chardevice_init(void);
void cresta_chardevice_cleanup(void);


void remove_device_entry(struct cresta_dev* crestadev);
void make_device_entry(struct cresta_dev* crestadev);

int cresta_dev_uevent(struct device *dev, struct kobj_uevent_env *env);



#endif