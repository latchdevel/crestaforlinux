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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include "cresta_chardevice.h"
#include "cresta_interrupthandler.h"
#include "cresta_sensor_mgmt.h"



//manchester decoder variables
static uint8_t  halfBit = 0; 			// 9 bytes of 9 bits each, 2 edges per bit = 162 halfbits for thermo/hygro
static uint32_t clockTime; 			// Measured duration of half a period, i.e. the the duration of a short edge.
//static uint32_t clockTime = 488; 			// Measured duration of half a period, i.e. the the duration of a short edge.

static bool     isOne;    			// true if the the last bit is a logic 1.
static uint8_t  packageLength;
static uint8_t  data[CRESTA_MAXDATA_LEN];	// Maximum number of bytes used by Cresta
static ktime_t *ts;				// timestamp for current execution
static ktime_t *lastChange;			// timestamp of last execution

static uint8_t halfBitCounter = ~0;

//GPIO & IRQ related
short int cresta_gpio_irq = 0;	// interrupt we're assigned to


struct kfifo_rec_ptr_1 irqtime_kfifo;
struct kfifo_rec_ptr_1 rawdata_kfifo;

static struct workqueue_struct *cresta_workqueue;
struct cresta_work *decryptwork;
struct cresta_work *manchester_work;

/*
 * Resets the manchester decoder
 */ 
void reset_manchester_decoder(uint32_t duration) {
    halfBit = 1;
    clockTime = duration >> 1;
    isOne = true;
    halfBitCounter = ~0;
}

 
/*
 * bottom half interrupt tasklet
 * reads time of IRQ and triggers manchester decoding
 */

void cresta_irq_bh(struct work_struct* work) {
  
 //get time of IRQ from FIFO 
  while(kfifo_out(&irqtime_kfifo, ts, sizeof(ktime_t))) {
    //calculate duration (time between last irq and current irq)
    //note: will be incorrect for very first execution, as lastChange = 0
    //but that's no problem for our scenario
    cresta_manchester_decoder((uint32_t) ktime_us_delta(*ts, *lastChange));
    *lastChange = *ts;
  }
}

void cresta_manchester_decoder (uint32_t duration) {
  /* I'll follow CrestaProtocol documentation here. However, I suspect it is inaccurate at some points:
  * - there is no stop-bit after every byte. Instead, there's a start-bit (0) before every byte.
  * - Conversely, there is no start-bit "1" before every byte.
  * - An up-flank is 0, down-flank is 1, at least with both my receivers.
  *
  * However, since the first start-bit 0 is hard to distinguish given the current clock-detecting
  * algorithm, I pretend there *is* a stop-bit 0 instead of start-bit. However, this means the
  * last stop-bit of a package must be ignored, as it simply isn't there.
  *
  * This manchester decoder is based on the principle that short edges indicate the current bit is the
  * same as previous bit, and that long edge indicate that the current bit is the complement of the
  * previous bit.
  */



  
  if (halfBit==0) {
    // Automatic clock detection. One clock-period is half the duration of the first edge.
    clockTime = duration >> 1;
    
    // Some sanity checking, very short (<200us) or very long (>1000us) signals are ignored.
    if (clockTime < 200 || clockTime > 1000) {
      //printk(KERN_NOTICE "Signal too short or too long: %d Ignoring\n", duration);
      goto out;
    }
    isOne = true;
  }
  else {
    // Edge is not too long, nor too short?
    if (duration < (clockTime >> 1) || duration > (clockTime << 1) + clockTime) { // read as: duration < 0.5 * clockTime || duration > 3 * clockTime
      // Fail. Abort.
      //printk(KERN_NOTICE "Edge is too long or too short. Resetting.\n");
      reset_manchester_decoder(duration);
      goto out;
    }

    // Only process every second half bit, i.e. every whole bit.
    if (halfBit & 1) {  
      uint8_t currentByte = halfBit / 18;
      uint8_t currentBit = (halfBit >> 1) % 9; // nine bits in a byte.
      if (currentBit < 8) {
	//make sure we don't write out of array
	if(currentByte < CRESTA_MAXDATA_LEN) {
	  if (isOne) {
	    // Set current bit of current byte
	    data[currentByte] |= 1 << currentBit;
	  } 
	  else {
	    // Reset current bit of current byte
	    data[currentByte] &= ~(1 << currentBit);
	  }
	}
      } else {
	// Ninth bit must be 0
	if (isOne) {
	  //printk(KERN_NOTICE "9th bit not 0. Resetting.\n");
	  // Bit is 1. Fail. Abort.
	  reset_manchester_decoder(duration);
	  goto out;
	}                    
      }


      if (halfBit == 17) { // First byte has been received
	// First data byte must be x75.
	if (data[0] != 0x75) {
	  //printk(KERN_NOTICE "got a byte, but != 0x75 header. Resetting\n");
	  reset_manchester_decoder(duration);
	  goto out;
	}
      } 
      else if (halfBit == 53) { // Third byte has been received
	// Obtain the length of the data
	uint8_t decodedByte = data[2]^(data[2]<<1);
	packageLength = (decodedByte >> 1) & 0x1f;

	// Do some checking to see if we should proceed
	if (packageLength < CRESTA_MIN_ANNOUNCED_LEN || packageLength > CRESTA_MAX_ANNOUNCED_LEN) {
	  //printk(KERN_NOTICE "Got length information, but length is invalid. Resetting\n");
	  reset_manchester_decoder(duration);
	  goto out;
	} else {
	  halfBitCounter = (packageLength + 3) * 9 * 2 - 2 - 1; // 9 bits per byte, 2 edges per bit, minus last stop-bit (see comment above)
	}
	//printk(KERN_NOTICE "Got length information. Length = %d. \n", packageLength);
      }
     // printk(KERN_INFO "writing data[%d]\n", currentByte);
     // printk(KERN_INFO "currentBit = %d\n", currentBit);


      // Done?
      if (halfBit >= halfBitCounter) {
	// schedule decrypting
      //keep the typecast for (uint8_t) ~0, or the check will fail
	if (halfBitCounter != (uint8_t) ~0) {
	  //last sanity checks. keep them in, as we still get garbage in very rare cases
	  uint8_t lengthSanity = data[2]^(data[2]<<1);
	  lengthSanity = (lengthSanity >> 1) & 0x1f;
	  if(data[0] == 0x75 && lengthSanity >= CRESTA_MIN_ANNOUNCED_LEN && lengthSanity <= CRESTA_MAX_ANNOUNCED_LEN) {
	    kfifo_in(&rawdata_kfifo, data, sizeof(data));
	    queue_work(cresta_workqueue, &decryptwork->ws);
	  } else {
	    printk(KERN_INFO "data[0] = %x\n", data[0]);
	    printk(KERN_INFO "packet length = %d\n", packageLength);
	    printk(KERN_INFO "packet length (final sanity check) = %d\n", lengthSanity);
	    printk(KERN_INFO "halfBitCounter = %d\n", halfBitCounter);
	    printk(KERN_INFO "halfBit = %d\n", halfBit);
	  }
	}
	// reset
	reset_manchester_decoder(duration);
	halfBit = 0;
	goto out;
      }
    }

    // Edge is long?
    if (duration > clockTime + (clockTime >> 1)) { // read as: duration > 1.5 * clockTime
      // Long edge.
      isOne = !isOne;
      // Long edge takes 2 halfbits
      halfBit++;
    }
  }

  halfBit++;
 // printk(KERN_INFO "halfBit = %d\n", halfBit);

   
   
out:   

  return;
}


/**
 * top half of the cresta IRQ irq handler
 */
static irqreturn_t cresta_irq_th(int irq, void *dev_id, struct pt_regs *regs) {
  //NOTE: since 2.6.35 IRQs are disabled by default while in an ISR
  ktime_t now = ktime_get();
  kfifo_in(&irqtime_kfifo, &now, sizeof(now));
  queue_work(cresta_workqueue, &manchester_work->ws);

  return IRQ_HANDLED;
}


 
 
/*
 * Sets up the GPIO pin for interrupt handling
 * NOTE: use new gpiod API once Kernel 3.13 is available, see
 * https://www.kernel.org/doc/Documentation/gpio/consumer.txt
 */ 
int setup_interrupt(void) {
 
   if (gpio_request(CRESTA_GPIO, CRESTA_GPIO_DESC)) {
      printk(KERN_ERR "GPIO request faiure: %s\n", CRESTA_GPIO_DESC);
      return -1;
   }
   
   if (gpio_direction_input(CRESTA_GPIO)) {
     printk(KERN_ERR "Failed to set GPIO as input\n");
     return -1;
   }
 
   if ( (cresta_gpio_irq = gpio_to_irq(CRESTA_GPIO)) < 0 ) {
      printk(KERN_ERR "GPIO to IRQ mapping faiure %s\n", CRESTA_GPIO_DESC);
      return -1;
   }
 
   printk(KERN_INFO "Mapped int %d\n", cresta_gpio_irq);
 
   //NOTE: CRESTA_GPIO_DEVICE_DESC is usually a dev_t
   //strictly we don't need this parameter, however, setting it
   //to NULL (and also to NULL in free_irq) resulted in kernel
   //panic when doing the following:
   //insmod cresta.ko -> rmmod cresta -> insmod cresta.ko -> panic
   if (request_irq(cresta_gpio_irq,
                   (irq_handler_t ) cresta_irq_th,
                   IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING,
                   CRESTA_GPIO_DESC,
                   CRESTA_GPIO_DEVICE_DESC)) {
      printk(KERN_ERR "IRQ request failure\n");
      return -1;
   }
 
   return 0;
}
 
 
/*
 * Releases GPIO and Interrupt
 */ 
void release_interrupt(void) {
   disable_irq(cresta_gpio_irq); //wait for ISR to complete
   free_irq(cresta_gpio_irq, CRESTA_GPIO_DEVICE_DESC);
   gpio_free(CRESTA_GPIO);
   enable_irq(cresta_gpio_irq);
 
   return;
}
 
 
/*
 * Module initialization
 */ 
int __init cresta_interrupthandler_init(void) {
  printk(KERN_NOTICE "Loading Cresta Module.\n");

  //initialize snesor management
  cresta_sensor_mgmt_init();
  
  //initialize character device handling
  cresta_chardevice_init();

  if(kfifo_alloc(&irqtime_kfifo, CRESTA_KFIFO_SIZE, GFP_KERNEL)) {
    printk(KERN_ERR "Error, couldn't allocate memory for FIFO buffer\n");
    goto err;
  }

  if(kfifo_alloc(&rawdata_kfifo, CRESTA_KFIFO_SIZE, GFP_KERNEL)) {
    printk(KERN_ERR "Error, couldn't allocate memory for FIFO buffer\n");
    goto err;
  }

  
  ts         = kzalloc(sizeof(ktime_t), GFP_KERNEL);
  lastChange = kzalloc(sizeof(ktime_t), GFP_KERNEL);
  

  if(NULL == ts || NULL == lastChange) {
    printk(KERN_NOTICE "Error, couldn't allocate memory for timepecs\n");
    goto err;
  }
  
  
  cresta_workqueue = alloc_workqueue(CRESTA_GPIO_DEVICE_DESC, WQ_NON_REENTRANT, 1);
  if (NULL == cresta_workqueue) {
    goto err;
  }
  
  
  decryptwork     = kmalloc(sizeof(struct cresta_work), GFP_KERNEL);
  manchester_work = kmalloc(sizeof(struct cresta_work), GFP_KERNEL);

  if(NULL == decryptwork || NULL == manchester_work) {
    goto err;
  }
  
  INIT_WORK(&decryptwork->ws, handle_encrypted_sensor_data);
  INIT_WORK(&manchester_work->ws, cresta_irq_bh);


  
  if(setup_interrupt()) {
    goto err;
  }


   return 0;
 
err:
   kfree(ts);
   kfree(lastChange);
   kfifo_free(&irqtime_kfifo);
   kfifo_free(&rawdata_kfifo);
   if(NULL != cresta_workqueue) {
     destroy_workqueue(cresta_workqueue);
   }
   kfree(decryptwork);
   kfree(manchester_work);
   cresta_sensor_mgmt_cleanup();
   cresta_chardevice_cleanup();
   return -1;
}

/*
 * Module cleanup
 */ 
void __exit cresta_interrupthandler_cleanup(void) {
   release_interrupt();
   kfree(ts);
   kfree(lastChange);
   
   //cleanup work queue
   flush_workqueue(cresta_workqueue);
   destroy_workqueue(cresta_workqueue);
   
   kfree(decryptwork);
   kfree(manchester_work);
   

   //cleanup sensor management
   //NOTE: call this before cresta_chardevice_cleanup
   //as only sensor management can identify individual
   //devices and remove them via interaction with
   //cresta_chardevice.*
   cresta_sensor_mgmt_cleanup();


   //cleanup character devices
   cresta_chardevice_cleanup();
   

   //free memory of fifos
   kfifo_free(&irqtime_kfifo);
   kfifo_free(&rawdata_kfifo);

   printk(KERN_NOTICE "Removed Cresta Module.\n");
   return;
}
 
 
module_init(cresta_interrupthandler_init);
module_exit(cresta_interrupthandler_cleanup);
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
