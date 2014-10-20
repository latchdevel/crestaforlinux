/*
 * License: GPLv3. See license.txt
 */
#ifndef _CRESTA_DECODER_H_
#define _CRESTA_DECODER_H_

#include <stdint.h>
#include "../cresta_common/cresta_common.h"

#define METRIC_UNITS 1

void print_measurement_data(struct cresta_measurement_data* data);
void print_measurement_data_short(struct cresta_measurement_data* data);

uint8_t get_preamble_from_decrypted_data(uint8_t* decrypted_data);
uint8_t get_sensor_address_from_decrypted_data(uint8_t* decrypted_data);
uint8_t get_packet_length_from_decrypted_data(uint8_t* decrypted_data);
uint8_t get_sensor_type_from_decrypted_data(uint8_t* decrypted_data);


int get_battery_low_status(uint8_t* decrypted_data);

float get_temperature_from_cresta_encoding(uint8_t* decrypted_data, uint8_t offset);
float get_thermohygro_temperature(uint8_t* decrypted_data);
int8_t get_thermohygro_humidity(uint8_t* decrypted_data);

float get_anemometer_temperature(uint8_t* decrypted_data);
float get_anemometer_windchill(uint8_t* decrypted_data);
float get_anemometer_windspeed(uint8_t* decrypted_data);
float get_anemometer_windgust(uint8_t* decrypted_data);
float get_anemometer_wind_direction(uint8_t* decrypted_data);

float get_uv_absolute_temperature(uint8_t* decrypted_data);
float get_uv_medh(uint8_t* decrypted_data);
float get_uv_uvindex(uint8_t* decrypted_data);
uint8_t get_uv_uvlevel(uint8_t* decrypted_data);

uint16_t get_rain_tick_count(uint8_t* decrypted_data);


char* get_temperature_string(uint8_t* decrypted_data, uint8_t offset);

char* get_thermohygro_temperature_string(uint8_t* decrypted_data);
char* get_thermohygro_humidity_string(uint8_t* decrypted_data);

char* get_anemometer_temperature_string(uint8_t* decrypted_data);
char* get_anemometer_windchill_string(uint8_t* decrypted_data);
char* get_anemometer_windspeed_string(uint8_t* decrypted_data);
char* get_anemometer_windgust_string(uint8_t* decrypted_data);
char* get_anemometer_wind_direction_string(uint8_t *decrypted_data);

char* get_uv_absolute_temperature_string(uint8_t* decrypted_data);
char* get_uv_medh_string(uint8_t* decrypted_data);
char* get_uv_uvindex_string(uint8_t* decrypted_data);
char* get_uv_uvlevel_string(uint8_t* decrypted_data);

char* get_rain_tick_count_string(uint8_t* decrypted_data);

#endif