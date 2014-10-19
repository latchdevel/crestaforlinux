#ifndef _CRESTA_COMMON_H_
#define _CRESTA_COMMON_H_

//maximum length of a datagram including prefix and checksum bytes
#define CRESTA_MAXDATA_LEN 14

//minimum length of sensor data announced in sensor datagrams
#define CRESTA_MIN_ANNOUNCED_LEN 6

//maximum length of sensor data announced in sensor datagrams
#define CRESTA_MAX_ANNOUNCED_LEN 11


/*
 * Sensors use dedicated address ranges. We can
 * identify the sensor type to some degree by
 * masking the sensor's address
 */ 
#define CRESTA_SENSOR_ADDR_MASK 0xE0
enum cresta_sensor_address_masks {
    CRESTA_AM_THERMOHYGRO_CH1 = 0x20,
    CRESTA_AM_THERMOHYGRO_CH2 = 0x40,
    CRESTA_AM_THERMOHYGRO_CH3 = 0x60,
    CRESTA_AM_RAIN_UV_ANEMO   = 0x80,
    CRESTA_AM_THERMOHYGRO_CH4 = 0xA0,
    CRESTA_AM_THERMOHYGRO_CH5 = 0xC0
};

/*
 * Sensor type is contained in each
 * sensor datagram
 */ 
enum cresta_sensor_type {
    CRESTA_SENSOR_TYPE_ANEMOMETER  = 0x0C,
    CRESTA_SENSOR_TYPE_UV          = 0x0D,
    CRESTA_SENSOR_TYPE_RAIN        = 0x0E,
    CRESTA_SENSOR_TYPE_THERMOHYGRO = 0x1E
};


/*
 * Stores all the data we export to user space
 * via character device. Originally, only the decrypted
 * sensor data was transmitted. However, this structure
 * allows addition of other data, e.g. time of measurement,
 * which isn't contained in the measurement data itself
 */
struct measurement {
  uint64_t measurement_time_seconds;
  uint8_t decrypted_data[CRESTA_MAXDATA_LEN];
};

/*
 * Extends measurement by some additional
 * fields for convenient access of data
 */
struct cresta_measurement_data {
    uint8_t sensor_address;	//for convenience
    uint8_t len;		//for convenience
    uint8_t sensor_type;	//for convenience
    struct measurement measurement;
};

#endif
