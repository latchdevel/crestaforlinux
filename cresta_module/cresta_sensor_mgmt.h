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

#ifndef _CRESTA_SENSOR_MGMT_H_
#define _CRESTA_SENSOR_MGMT_H_

#include "cresta_chardevice.h"

int                cresta_sensor_mgmt_init(void);
void               cresta_sensor_mgmt_cleanup(void);

void               handle_encrypted_sensor_data(struct work_struct*);
int                handle_decrypted_sensor_data(struct cresta_measurement_data*);
uint8_t            second_check(uint8_t);
bool               decrypt_and_check(uint8_t*);
int                update_cresta_sensor_data(struct cresta_dev*, struct cresta_measurement_data*);
struct cresta_dev* get_cresta_sensor_by_address(uint8_t);
struct cresta_dev* create_cresta_sensor(uint8_t, uint8_t);
void               delete_cresta_sensor(struct cresta_dev*);
int                add_cresta_sensor_to_sensor_list(struct cresta_dev*);
uint8_t            get_preamble_from_decrypted_data(uint8_t* decrypted_data);
uint8_t            get_sensor_address_from_decrypted_data(uint8_t* decrypted_data);
uint8_t            get_packet_length_from_decrypted_data(uint8_t* decrypted_data);
uint8_t            get_sensor_type_from_decrypted_data(uint8_t* decrypted_data);


#endif
