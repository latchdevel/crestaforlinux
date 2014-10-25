#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include "cresta_decoder.h"


int main(int argc, char*argv[]) {
  long filesize = 0;
  
  
  int shortoutput = 0;
  char *filename = NULL;
  int c;

  opterr = 0;

  while ((c = getopt (argc, argv, "sc:")) != -1) {
    switch (c) {
      case 's': {
        shortoutput = 1;
        break;
      }
      case 'c': {
        filename = optarg;
        break;
      }
      case '?': {
        if (optopt == 'c')
          fprintf (stderr, "Option -%c requires cresta device file as an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);
        return 1;
      }
      default: {
        abort ();
      }
    }
  }


  if(NULL == filename) {
    printf("Usage: %s [-s]-c <devicefile>\n", argv[0]);
    printf("\t-c devicefile\tThe cresta character device to read from\n");
    printf("\t-s\t\tOnly output raw values. Values are separated\n");
    printf("\t\t\tby \":\", if multiple values per sensor\n");
    return -1;
  }
  
   FILE *fp = fopen(filename,"r");
   
   
 
 
 
  if(NULL == fp) {
    printf("Couldn't open file.\n");
    return -1;
  }

  fseek(fp, 0, SEEK_END);
  filesize = ftell(fp);
  if(filesize > sizeof(struct measurement)) {
    printf("Invalid measurement data length: %ld\n", filesize);
    fclose(fp);
    return -1;
  }
   
  fseek(fp, 0, SEEK_SET);

  struct cresta_measurement_data *sensor_data = calloc(sizeof(struct cresta_measurement_data), 1);

  //struct measurement *raw_measurement_data = calloc(sizeof(struct measurement), 1);
  fread(&sensor_data->measurement, filesize, 1, fp);

  fclose(fp);
   
  //sensor_data->measurement = raw_measurement_data;
  sensor_data->sensor_address = get_sensor_address_from_decrypted_data(sensor_data->measurement.decrypted_data);
  sensor_data->len            = get_packet_length_from_decrypted_data(sensor_data->measurement.decrypted_data);
  sensor_data->sensor_type    = get_sensor_type_from_decrypted_data(sensor_data->measurement.decrypted_data);
  
  if(shortoutput) {
    print_measurement_data_short(sensor_data);
  } else {
    print_measurement_data(sensor_data);
  }
  
  
  //free(raw_measurement_data);
  free(sensor_data);

 
 
 
 return 0;
 
 
}



uint8_t get_preamble_from_decrypted_data(uint8_t* decrypted_data) {
    return decrypted_data[0];
}

uint8_t get_sensor_address_from_decrypted_data(uint8_t* decrypted_data) {
    return decrypted_data[1];
}

uint8_t get_packet_length_from_decrypted_data(uint8_t* decrypted_data) {
    /*
     * According to cresta.pdf: bits 5..1 hold packet length. Bits 6&7
     * seme to be always 1. Bit 0 seems always to be 0
     * acual data stream with preamble is 1 longer -> +1
     */
    return ((decrypted_data[2] >> 1) & 0x1F) + 1;
}

uint8_t get_sensor_type_from_decrypted_data(uint8_t* decrypted_data) {
    /*
     * According to cresta.pdf: bits 6 and 5 reflect packet number in
     * stream. Bit 4..0 contain device type
     * 
     */
    return (decrypted_data[3] & 0x1F);
}



 

/**
 * Several sensors use same encoding, however have a different
 * offset for the values in their data. We use this "generic"
 * function to avoid code duplication.
 * 
 * Offset is index of lowest byte containing temperature data.
 */
float get_temperature_from_cresta_encoding(uint8_t* decrypted_data, uint8_t offset) {
      float temperature = 0;

      /**
       * according to cresta.pdf temperature bcd encoded
       * 1st digit: byte[n+1], lower nibble
       * 2nd digit: byte[n],   higher nibble
       * 3rd digit: byte[n],   lower nibble
       * 
       * sign:
       * byte[n+1], high nibble (maybe + additional mask?)
       *            = 0x04 -> negastive
       *            = 0x0C -> positive
       */ 
      
      temperature += ((decrypted_data[offset+1] & 0x0F) * 10);
      temperature += (decrypted_data[offset] >> 4);
      temperature += ((decrypted_data[offset] & 0x0F) / 10);
      
      
      //check whether temperature is positive or negative
      if((decrypted_data[offset+1] >> 4) == 0x04) {
	//temperature is negative
	temperature *= -1;
      } else if((decrypted_data[offset+1] >> 4) == 0x0C) {
	//do nothing
      } else {
	printf("Unexpected value for temperature sign: %x\n", (decrypted_data[offset+1] >> 4));
      }
      return temperature;
}


/**
 * Returns temperature. We avoid floating point, by returning temperature in
 * degree celsius * 10
 */
float get_thermohygro_temperature(uint8_t* decrypted_data) {
      /*
       * Temperature is BCD encoded.
       * byte[4] holds last two digits of temperature
       * byte[5] holds sign and first digit of temperature
       */
    return get_temperature_from_cresta_encoding(decrypted_data, 4);
}

int8_t get_thermohygro_humidity(uint8_t* decrypted_data) {
    int8_t humidity = 0;
      /*
       * humidity is BCD encoded
       * byte[6] holds humidity
       */
      humidity += ((decrypted_data[6] >> 4) * 10);
      humidity += (decrypted_data[6] & 0x0F);
      
    
    return humidity;
}



float get_anemometer_temperature(uint8_t* decrypted_data) {
      /*
       * Temperature is BCD encoded.
       * byte[4] holds last two digits of temperature
       * byte[5] holds sign and first digit of temperature
       */
      return get_temperature_from_cresta_encoding(decrypted_data, 4);
}

float get_anemometer_windchill(uint8_t* decrypted_data) {
    /*
     * Windchill is BCD encoded in byte[6] and byte[7]
     */
    return get_temperature_from_cresta_encoding(decrypted_data, 6);

}

float get_anemometer_windspeed(uint8_t* decrypted_data) {
    float windspeed = 0;
    
    /*
     * Windspeed: BCD encoded, miles per hour
     * 1st digit: byte[9], lower nibble 
     * 2nd digit: byte[8], upper nibble
     * 3rd digit: byte[8], lower nibble
     */
    
	windspeed += (decrypted_data[9] & 0x0F) * 10;
	windspeed += (decrypted_data[8] >> 4);
	windspeed += (decrypted_data[8] & 0x0F) / 10;
	
    if(METRIC_UNITS) {
      windspeed *= 1.60934; 
    }
    
    return windspeed;
}

float get_anemometer_windgust(uint8_t* decrypted_data) {
    float windgust = 0;
    
    /*
     * Windgust: BCD encoded, mph
     * 1st digit: byte[10], upper nibble
     * 2nd digit: byte[10], lower nibble
     * 3rd digit: byte[9], upper nibble
     */
      windgust += (decrypted_data[10] >> 4) * 10;
      windgust += (decrypted_data[10] & 0x0F);
      windgust += (decrypted_data[9] >> 4) / 10;
      
    if(METRIC_UNITS) {
      windgust *= 1.60934; 
    }
    
    return windgust;
}

float get_anemometer_wind_direction(uint8_t* decrypted_data) {
    /*
     * direction in byte[11]
     * decoding code from cresta.pdf
     */
    uint8_t count = decrypted_data[11] >> 4;
    float direction = 22.5;
    
    count ^= (count & 8) >> 1;
    count ^= (count & 4) >> 1;
    count ^= (count & 2) >> 1;
    count = -count & 0xF;
    

    return direction*count;

}

float get_uv_absolute_temperature(uint8_t* decrypted_data) {
  /**
   * accordig to cresta.pdf, UV sensor uses special temperature
   * format, not having a sign, thus only giving only
   * absolute temperature values
   *
   * 1st digit: byte[5], low nibble
   * 2nd digit: byte[4], high nibble
   * 3rd digit: byte[4], low nibble
   */
   
  float temperature = 0;
  
  temperature += (decrypted_data[5] & 0x0F) * 10;
  temperature += (decrypted_data[4] >> 4);
  temperature += (decrypted_data[4] & 0x0F) / 10;

  return temperature;
}

/**
 * Note: to avoid floating point, value is scaled by 10
 */
float get_uv_medh(uint8_t* decrypted_data) {
  /*
   * according to cresta.pdf, medh is BCD coded
   * 
   * 1st digit: byte[6], high nibble
   * 2nd digit: byte[6], low nibble
   * 3rd digit: byte[5], high nibble
   */ 

  float medh = 0;
  
  medh += (decrypted_data[6] >> 4) * 10;
  medh += (decrypted_data[6] & 0x0F);
  medh += (decrypted_data[5] >> 4) / 10;

  return medh;
  
}

/**
 * Note: to avoid floating point, value is scaled by 10
 */
float get_uv_uvindex(uint8_t* decrypted_data) {
  /**
   * cresta.pdf: BCD encoded.
   * 1st digit: byte[8], low nibble
   * 2nd digit: byte[7], high nibble
   * 3rd digit: byte[7], low nibble
   */
  
  float uv_index = 0;
  uv_index += (decrypted_data[8] & 0x0F) * 10;
  uv_index += (decrypted_data[7] >> 4);
  uv_index += (decrypted_data[7] & 0x0F) / 10;

  return uv_index;
}

/**
 * According to cresta, following meaning:
 *     UV Index      UV Level
 * 0   0.0 - 2.9     LOW
 * 1   3.0 - 5.9     MEDIUM
 * 2   6.0 - 7.9     HIGH
 * 3   8.0 - 10.9    VERY HIGH
 * 4   above 10.9    EXTREMELY HIGH
 */ 
uint8_t get_uv_uvlevel(uint8_t* decrypted_data) {
   /**
    * according to cresta.pdf UV level encoded in
    * byte[8], high nibble
    */
   
   uint8_t uv_level = 0;
      uv_level = decrypted_data[8] >> 4;
  return uv_level;
}


uint16_t get_rain_tick_count(uint8_t* decrypted_data) {
  /* according to cresta.pdf
   * rain ticks are encoded BINARY (not BCD)
   * most significant byte: byte[5];
   * least significant byte: byte[4];
   */
  
    uint16_t rain_ticks = 0;
      rain_ticks = (decrypted_data[5] << 8);
      rain_ticks |= decrypted_data[4];
    
    return rain_ticks;
}

int get_battery_status(uint8_t* decrypted_data) {
  //according to cresta PDF:
  //
  //bits 7+6 of byte[2] (length) encode battery status
  //Battery OK: both 1
  //Battery < 2.5V: both 0
  int is_good = 0;
  
  uint8_t status = (decrypted_data[2] >> 6) & 0x03;
  if(status == 0x03) {
    is_good = 1;
  } else {
    is_good = 0;
  }
  
  return is_good;
}
  


void print_measurement_data(struct cresta_measurement_data* data) {
    switch(data->sensor_type) {
      case(CRESTA_SENSOR_TYPE_ANEMOMETER): {
	printf("Anenometer sensor data:\n");
	printf("\tTime = %s",  ctime((time_t*)&data->measurement.measurement_time_seconds));
	printf("\tTemperature = %.01f °C\n", get_anemometer_temperature(data->measurement.decrypted_data));
	printf("\tWind chill = %.01f °C\n", get_anemometer_windchill(data->measurement.decrypted_data));
	printf("\tWind speed = %.02f km/h\n", get_anemometer_windspeed(data->measurement.decrypted_data));
	printf("\tWind gust = %.02f km/h\n", get_anemometer_windgust(data->measurement.decrypted_data));
	printf("\tWind direction = %.01f °\n", get_anemometer_wind_direction(data->measurement.decrypted_data));
	if(get_battery_status(data->measurement.decrypted_data)) {
	  printf("\tBattery = OK\n");
	} else {
	  printf("\tBattery = LOW\n");
	}
	break;
      }
      case(CRESTA_SENSOR_TYPE_UV): {
	printf("UV sensor data:\n");
	printf("\tTime = %s",  ctime((time_t*)&data->measurement.measurement_time_seconds));
	printf("\tAbsolute temperature = %.01f °C\n", get_uv_absolute_temperature(data->measurement.decrypted_data));
	printf("\tUV med/h = %.01f\n", get_uv_medh(data->measurement.decrypted_data));
	printf("\tUV index = %.01f\n", get_uv_uvindex(data->measurement.decrypted_data));
	printf("\tUV level = %d\n", get_uv_uvlevel(data->measurement.decrypted_data));
	if(get_battery_status(data->measurement.decrypted_data)) {
	  printf("\tBattery = OK\n");
	} else {
	  printf("\tBattery = LOW\n");
	}
	break;
      }
      case(CRESTA_SENSOR_TYPE_RAIN): {
	printf("Rain sensor data:\n");
	printf("\tTime = %s",  ctime((time_t*)&data->measurement.measurement_time_seconds));
	printf("\train ticks = %d\n", get_rain_tick_count(data->measurement.decrypted_data));
	if(get_battery_status(data->measurement.decrypted_data)) {
	  printf("\tBattery = OK\n");
	} else {
	  printf("\tBattery = LOW\n");
	}
	break;
      }
      case(CRESTA_SENSOR_TYPE_THERMOHYGRO): {
	printf("ThermoHygro sensor data:\n");
	printf("\tTime = %s",  ctime((time_t*)&data->measurement.measurement_time_seconds));
	printf("\tTemperature = %.01f °C\n", get_thermohygro_temperature(data->measurement.decrypted_data));
	printf("\tHumidity = %d %%\n", get_thermohygro_humidity(data->measurement.decrypted_data));
	if(get_battery_status(data->measurement.decrypted_data)) {
	  printf("\tBattery = OK\n");
	} else {
	  printf("\tBattery = LOW\n");
	}
	break;
      }
      default: {
	printf("Unknown sensor type: %x\n", data->sensor_type);
      }
    }
}

void print_measurement_data_short(struct cresta_measurement_data* data) {
    switch(data->sensor_type) {
      case(CRESTA_SENSOR_TYPE_ANEMOMETER): {
	printf("%lu:%.01f:%.01f:%.02f:%.02f:%.01f:%d\n",(unsigned long) data->measurement.measurement_time_seconds
	                                        , get_anemometer_temperature(data->measurement.decrypted_data)
	                                        , get_anemometer_windchill(data->measurement.decrypted_data)
						, get_anemometer_windspeed(data->measurement.decrypted_data)
						, get_anemometer_windgust(data->measurement.decrypted_data)
						, get_anemometer_wind_direction(data->measurement.decrypted_data)
						, get_battery_status(data->measurement.decrypted_data));
	break;
      }
      case(CRESTA_SENSOR_TYPE_UV): {
	printf("%lu:%.01f:%.01f:%.01f:%d:%d\n",(unsigned long) data->measurement.measurement_time_seconds
	                               , get_uv_absolute_temperature(data->measurement.decrypted_data)
				       , get_uv_medh(data->measurement.decrypted_data)
				       , get_uv_uvindex(data->measurement.decrypted_data)
				       , get_uv_uvlevel(data->measurement.decrypted_data)
				       , get_battery_status(data->measurement.decrypted_data));
	break;
      }
      case(CRESTA_SENSOR_TYPE_RAIN): {
	printf("%lu:%d:%d\n", (unsigned long) data->measurement.measurement_time_seconds
	                 , get_rain_tick_count(data->measurement.decrypted_data)
			 , get_battery_status(data->measurement.decrypted_data));
	break;
      }
      case(CRESTA_SENSOR_TYPE_THERMOHYGRO): {
	printf("%lu:%.01f:%d:%d\n",(unsigned long) data->measurement.measurement_time_seconds
	                      , get_thermohygro_temperature(data->measurement.decrypted_data)
			      , get_thermohygro_humidity(data->measurement.decrypted_data)
			      , get_battery_status(data->measurement.decrypted_data));
	break;
      }
    }
}
