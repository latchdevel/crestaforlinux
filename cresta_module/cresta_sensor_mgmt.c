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

#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include "cresta_sensor_mgmt.h"

struct list_head cresta_sensor_list;
LIST_HEAD(cresta_sensor_list);
static DEFINE_MUTEX(mod_sensor_list_mutex);


extern struct kfifo_rec_ptr_1 irqtime_kfifo;
extern struct kfifo_rec_ptr_1 rawdata_kfifo;

/*
 * Initializes the part of the kernel module responsible
 * for decoding of data
 */ 
int cresta_sensor_mgmt_init() {
     mutex_init(&mod_sensor_list_mutex);
     
     return 0;
}

void cresta_sensor_mgmt_cleanup() {

    struct cresta_dev* ret = NULL;
    struct cresta_dev* cursor = NULL;

    mutex_lock(&mod_sensor_list_mutex);
    list_for_each_entry_safe(ret, cursor, &cresta_sensor_list, list) {
	list_del(&ret->list);
	remove_device_entry(ret); //delete the /dev entries
	delete_cresta_sensor(ret); //free the memory
    }	
    mutex_unlock(&mod_sensor_list_mutex);
}



/*
 * Decrypts the encrypted sensor data and calls 
 * handle_decrypted_sensor_data for further processing
 */
void handle_encrypted_sensor_data(struct work_struct* work) {
  struct timespec measurement_time;
 //get time of IRQ from FIFO
  while(!kfifo_is_empty(&rawdata_kfifo)) {
     struct cresta_measurement_data *sensor_data = kzalloc(sizeof(struct cresta_measurement_data), GFP_KERNEL);
     if(NULL != sensor_data) {
	if(kfifo_out(&rawdata_kfifo, sensor_data->measurement.decrypted_data, sizeof(sensor_data->measurement.decrypted_data)) == sizeof(sensor_data->measurement.decrypted_data)) {
	  //printk(KERN_INFO "data[0] = %x\n", sensor_data->measurement.decrypted_data[0]);
	  if(!decrypt_and_check(sensor_data->measurement.decrypted_data)) {
	     // printk(KERN_INFO "Copied %d bytes.\n", sizeof(sensor_data->measurement.decrypted_data));
	      sensor_data->sensor_address = get_sensor_address_from_decrypted_data(sensor_data->measurement.decrypted_data);
	      sensor_data->len            = get_packet_length_from_decrypted_data(sensor_data->measurement.decrypted_data);
	      sensor_data->sensor_type    = get_sensor_type_from_decrypted_data(sensor_data->measurement.decrypted_data);
	      getnstimeofday(&measurement_time);
	      sensor_data->measurement.measurement_time_seconds = measurement_time.tv_sec;
	      if(handle_decrypted_sensor_data(sensor_data)) {
		//an error occured
		kfree(sensor_data);
	      }
	  } else {
	    //decrypt failed
	    //printk(KERN_INFO "Decryption failed\n");
	    kfree(sensor_data);
	  }
	} else {
	  //fifo returned less bytes than requested 
	  printk(KERN_ERR "Error, kfifo didn't return a complete measurement record\n");
	  kfree(sensor_data);
	}
     } else {
       //out of memory
     }
  }

  //kfree(cwork);
}

/*
 * Helper routine for checking decrypted data
 */ 
uint8_t second_check(uint8_t b) { 
    uint8_t c;

    if (b&0x80) {
	b^=0x95;
    }
    c = b^(b>>1); 
    if (b&1) {
	c^=0x5f; 
    }
    
    if (c&1) {
	b^=0x5f;
    }
    
    return b^(c>>1); 
}

/*
 * Helper routine for decrypting data
 */
bool decrypt_and_check(uint8_t* raw_data) {
  
    uint8_t cs1,cs2,i; 
    uint8_t decodedByte;
    uint8_t packet_length;
    cs1=0; 
    cs2=0;
    decodedByte = raw_data[2]^(raw_data[2]<<1);
    packet_length = (decodedByte >> 1) & 0x1f;
    if(packet_length >= CRESTA_MIN_ANNOUNCED_LEN && packet_length <= CRESTA_MAX_ANNOUNCED_LEN) {
      
      //printk(KERN_INFO "Packet length = %d.\n", packet_length);
      
      
      for (i=1; i<packet_length+2; i++) { 
	  cs1^=raw_data[i]; 
	  cs2 = second_check(raw_data[i]^cs2); 
	  raw_data[i] ^= raw_data[i] << 1; 
      } 

      if (cs1) {
	  return -1; 
      }

      if (cs2 != raw_data[packet_length+2]) {
	  return -1;
      }
    
      return 0;
    } else {
      printk (KERN_INFO "Bogus packet length: %d. aborting decoding\n", packet_length);
      return -1;
    }
}

/*
 * Does most of the work regarding sensor data processing.
 *   - determines sensor for handling data
 *   - if no sensor found, new one will be created
 *   - updates data of sensor responsible for handling the data
 */
int handle_decrypted_sensor_data(struct cresta_measurement_data *data) {
    int success = 0;

    //get the sensor
    struct cresta_dev* sensor = get_cresta_sensor_by_address(data->sensor_address);
    if(NULL == sensor) {
	printk(KERN_INFO "Received data of new sensor. Asking for device creation.\n");
	sensor = create_cresta_sensor(data->sensor_address, data->sensor_type);
	if(NULL == sensor) {
	    printk(KERN_ERR "Device creation request failed. Aborting.\n");
	    success = -1;
	    return success;
	} else {
	    if(add_cresta_sensor_to_sensor_list(sensor)) {
		printk(KERN_ERR "Adding new device to device list failed. Aborting.\n");
		delete_cresta_sensor(sensor);
		success = -1;
		return success;
	    } else {
	        //add the character device and entries in /dev
	        make_device_entry(sensor);
//		printk(KERN_INFO "Adding new device to device list succeeded\n");
	    }
	}
    }

    //if we got this far, we have a sensor, that can handle the measurement data
    success = update_cresta_sensor_data(sensor, data);
    return success;
}

/*
 * Updates the sensor data of a sensor. Takes care of locking for
 * thread safe update of data
 */
int update_cresta_sensor_data(struct cresta_dev* sensor, struct cresta_measurement_data *new_data) {
    struct cresta_measurement_data *old_data = NULL;
    mutex_lock(&(sensor->measurement_data_mutex));
    old_data = sensor->current_data;
    rcu_assign_pointer(sensor->current_data, new_data);
    mutex_unlock(&(sensor->measurement_data_mutex));
    synchronize_rcu(); /* Wait for grace period. */
    kfree(old_data);
    //printk(KERN_INFO "Measurement data updated\n");
    return 0;
}

/*
 * Tries to retrieve the sensor with given address from
 * Internal sensor list. If sensor doesn't exist (e.g. because
 * it was just turned on), NULL is returned
 */
struct cresta_dev* get_cresta_sensor_by_address(uint8_t sensor_addr) {
    struct cresta_dev* ret = NULL;
    bool found = false;

    rcu_read_lock();
    list_for_each_entry_rcu(ret, &cresta_sensor_list, list) {
	if(ret->sensor_addr == sensor_addr) {
	    found = true;
	    break;
	}
    }
    rcu_read_unlock();

    if(!found) {
	//printk(KERN_INFO "No sensor found with address %x.\n", sensor_addr);
	ret = NULL;
    }


    return ret;
}

/*
 * Creates a new sensor and fills in some internal meta information.
 * Does NOT add sensor to sensor list (done explicitly after calling
 * this function
 */
struct cresta_dev* create_cresta_sensor(uint8_t sensor_addr, uint8_t sensor_type) {
    struct cresta_dev* new_sensor = kmalloc(sizeof(struct cresta_dev), GFP_KERNEL);
    if(NULL != new_sensor) {
	new_sensor->sensor_addr = sensor_addr;
	new_sensor->sensor_type = sensor_type;
	mutex_init(&(new_sensor->measurement_data_mutex));
	new_sensor->current_data = NULL;
    } else {
	printk(KERN_ERR "Cannot create cresta device. Out of memory.\n");
    }

    return new_sensor;
}

/*
 * We store sensors in an internal list. Whenever we receive data of
 * a sensor we haven't seen yet, we create a new one and add it
 * to our internal list.
 * This helper function takes care of locking the sensor list, so that
 * it can be modified in a thread safe manner
 */
int add_cresta_sensor_to_sensor_list(struct cresta_dev* new_sensor) {
/*
 * Due to multithreading we might run into following situation:
 *
 * Thread A and B both try to add a new sensor with same address to list
 * (this scenario is likely:
     - sensors send measurement data 3 times in with 10ms interarrival time
     - if we don't find a sensor device for handling data, we create a new one
     -> multiple threads might try to create a new device)
 * 
 * Both thread A and B will determine that a new device has to be created
 * Both allocate memory (new_device_a, new_device_b)
 * Thread A will get lock to add new_device_a and will succeed
 * Thread B will wait for a to finish, then try to add new_device_b
 *   Now we could do a sanity check in the add_list function, whether
 *   there is already an entry for new_device_b->sensor_addr and abort
 *   insertion to list, if this is true.
 *   However, then Thread B needs a reference to the already existing
 *   new_device_a. We could do that as a return value of add_list.
 *   However, then the code would always have to use the return value of
 *   add_list instead of pointer provied by create_sensor.
 *   In our example, we furthermore would have to take care of freeing memory,
 *   in case the new_device_b pointer we provided to add_list is different
 *   from the returned pointer of add_list (new_device_a)
 *   Is probably very error prone, hence following solution:
 *
 *   We DO make a sanity check in add_cresta_sensor_to_sensor_list, to see
 *   whether the new sensor's address is already handled. If that is the
 *   case, we return a negative value, indicating an error
 *   The caller should then in case of an error free up the memory of the
 *   sensor, that couldn't be added to the list, and abort.
 *   For our use case, this behavior should be completely fine, as these
 *   race conditions typically occur when identical sensor data is
 *   received multiple times
 *
 */
    int success = 0;
    if(NULL != new_sensor) {
        mutex_lock(&mod_sensor_list_mutex);
	//due to multi threading we have to do an additional check for
        //presence in list right here
	if(get_cresta_sensor_by_address(new_sensor->sensor_addr)) {
	    printk(KERN_WARNING "Not adding sensor to list, already present.\n");
	    success = -1;
	} else {
	    printk(KERN_INFO "Adding new sensor to list.\n");
	    list_add_rcu(&(new_sensor->list), &cresta_sensor_list);
	}
        mutex_unlock(&mod_sensor_list_mutex);
    } else {
	printk(KERN_ERR "Sensor device is NULL. Not adding to sensor list.\n");
	success = -2;
    }
    return success;
}


/*
 * Precondition: sensor isn't in sensor list any more
 * Deletes a sensor. Currently only freeing memory and
 * measurement data of sensor
 */
void delete_cresta_sensor(struct cresta_dev* sensor) {
    if(NULL != sensor->current_data) {
      kfree(sensor->current_data);
    }
    kfree(sensor);
}



/*
 * Extracts sensor data from decrypted data
 */
uint8_t get_sensor_address_from_decrypted_data(uint8_t* decrypted_data) {
    return decrypted_data[1];
}

/*
 * Extracts packet lenth from decrypted data
 */
uint8_t get_packet_length_from_decrypted_data(uint8_t* decrypted_data) {
    /*
     * According to cresta.pdf: bits 5..1 hold packet length. Bits 6&7
     * seme to be always 1. Bit 0 seems always to be 0
     * acual data stream with preamble is 1 longer -> +1
     */
    return ((decrypted_data[2] >> 1) & 0x1F) + 1;
}

/*
 * Extracts sensor type from decrypted data
 */
uint8_t get_sensor_type_from_decrypted_data(uint8_t* decrypted_data) {
    /*
     * According to cresta.pdf: bits 6 and 5 reflect packet number in
     * stream. Bit 4..0 contain device type
     * 
     */
    return (decrypted_data[3] & 0x1F);
}
