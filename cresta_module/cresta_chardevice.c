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


#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#include <asm/uaccess.h>


#include "cresta_chardevice.h"
#include "cresta_interrupthandler.h"


struct cresta_sensor_counts* sensor_counts;
static struct class* cresta_class;
static int major;
static int minors;



/*
 * Initializes the part of the kernel module responsible
 * for character device handling
 */ 
int cresta_chardevice_init() {
  
    /*
     * request major and minor device id
     */
    int error;
    dev_t dev;
    
     sensor_counts = kzalloc(sizeof(struct cresta_sensor_counts), GFP_KERNEL);
     if(NULL == sensor_counts) {
       printk(KERN_ERR "Cannot allocate memory for sensor_counts\n");
       return -1;
     } 

    
    error = alloc_chrdev_region(&dev, 0, CRESTA_MAX_SENSOR_COUNT, "cresta");
    
    if(!error) {
      major = MAJOR(dev);
      minors = CRESTA_MAX_SENSOR_COUNT;
      //printk(KERN_INFO "Got major number %d\n", major);
    } else {
      printk(KERN_ERR "Failed to get cresta device numbers\n");
      return -1;
    }
    cresta_class = class_create(THIS_MODULE, "cresta");
    cresta_class->dev_uevent = cresta_dev_uevent;
    return 0;
}

/*
 * Cleans up the part of the kernel module responsible
 * for character device handling
 */
void cresta_chardevice_cleanup() {
    if(major) {
      unregister_chrdev_region(MKDEV(major, 0), minors);
    }
    
     kfree(sensor_counts);
     class_destroy(cresta_class);
     cresta_class = NULL;
}

/*
 * Open the sensor character device file
 */ 
int cresta_open(struct inode *inode, struct file *filp)
{
  /*
   * What we do here sucks big time:
   * 
   * Basic problem is, that we don't want the sensor data
   * to get overwritten while we read it.
   * A read might be done in a "byte-by-byte" fashion,
   * e.g. by many individual read calls.
   * This means, that setting a RCU lock at start of cresta_read call
   * and unsetting it at the end of each cresta_read call doesn't work, because
   * between reads, the writer can get the RCU "lock" and change
   * data. This would lead to inconsistent reads.
   * 
   * Also: RCU lock in cresta_open() and unlock in cresta_release() probably won't work
   * due to the mechanism of the grace period (which imho doesn't take into account
   * context switches to user space).
   * What probably _would_ work are reader/writer locks.
   * Reader lock set in cresta_open(), unset in cresta_close(). However,
   * this might block writer in update_cresta_sensor_data().
   * 
   * As I don't want the update to be blocked by the userspace read access,
   * I decided to take the following approach: make a per reader copy of the data
   * in cresta_read() and free memory of copy in cresta_close().
   * Although this is very innefficient, it's appropriate imho for the problem
   * at hand, as we only transfer a few bytes.
   */ 

  struct cresta_dev *dev = container_of(inode->i_cdev, struct cresta_dev, cdev);
  struct measurement *reader_copy = kmalloc(sizeof(struct measurement), GFP_KERNEL);
  struct cresta_measurement_data *data = NULL;
  rcu_read_lock();
  data = rcu_dereference(dev->current_data);
  /*
   * We have a race condition here: if a sensor was just created and the code
   * for updating the sensor data wasn't executed yet, measurement data
   * can be NULL. Although very unlikely, we catch this here
   */
  if(NULL == data) {
    kfree(reader_copy);
    return -ERESTARTSYS;
  } else if (NULL != reader_copy) {
    memcpy(reader_copy, &data->measurement, sizeof(struct measurement));
  }
  rcu_read_unlock();
  
  filp->private_data = reader_copy;

  return 0;
}

/*
 * Seek in the character device file
 */
loff_t cresta_llseek(struct file *filp, loff_t off, int whence)
{
  loff_t newpos;

  switch(whence) {
    case 0: { /* SEEK_SET */
      newpos = off;
      break;
    }

    case 1: { /* SEEK_CUR */
      newpos = filp->f_pos + off;
      break;
    }

    case 2: { /* SEEK_END */
      //NOTE: we always copy complete measurement struct, even if
      //acutal data of current sensor is < CRESTA_MAXDATA_LEN
      newpos = sizeof(struct measurement) + off;
      break;
    }

    default: {
      return -EINVAL;
    }
  }
  
  if (newpos < 0) return -EINVAL;
    filp->f_pos = newpos;
  return newpos;
}

/*
 * Release character device file (called on userspace close(file*)
 */

int cresta_release(struct inode *inode, struct file *filp)
{
  struct measurement *reader_copy = (struct measurement*) filp->private_data;
  kfree(reader_copy);
  return 0;
}

/*
 * Read from character device file and copy data to userspace
 */
ssize_t cresta_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
  struct measurement *data = (struct measurement*) filp->private_data;
  ssize_t retval = 0;
  uint8_t maxbytes = sizeof(struct measurement);
  if (*f_pos >= maxbytes)
    goto out;
  
  //note: we have to change this when we add timestamp to raw data
  if (*f_pos + count > maxbytes) {
    count = maxbytes - *f_pos;
  }
  if(copy_to_user(buf, (void*)(data) + *f_pos, count)) {
    retval = -EFAULT;
    goto out;
  }
  *f_pos += count;
  retval = count;

  out:
	return retval;
}

/*
 * File operations the cresta character devices support
 */ 
struct file_operations cresta_fops = {
	.owner =    THIS_MODULE,
	.llseek =   cresta_llseek,
	.read =     cresta_read,
	.open =     cresta_open,
	.release =  cresta_release,
};


int cresta_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0444);
    return 0;
}

/*
 * Register character device and create entry in /dev
 */
void make_device_entry(struct cresta_dev* crestadev) {
  int error = 0; 
 
  cdev_init(&crestadev->cdev, &cresta_fops);
  crestadev->cdev.owner = THIS_MODULE;
  crestadev->cdev.ops = &cresta_fops;
  
  crestadev->dev = MKDEV(major, crestadev->sensor_addr); //sensor addresses are unique, we use it for minor number
  error = cdev_add(&crestadev->cdev, crestadev->dev, 1);
  if(error) {
    printk(KERN_INFO "Error during cdev_add\n");
  } else {
    //for showing up in /dev/...
    
    /*
     * Determine device name. Goal is to keep it consistent with 
     * naming scheme of weather station (e.g. temperature sensors
     * numbered by channel.
     * However: theoretically we could support up to 255 devices, e.g.
     * by operating multiple temperature sensors on same channel. As this
     * operation mode isn't intended by device manufacturer, we loosen
     * device naming policies in that case. Example: two temperature sensors (A+B)
     * operating on channel one: one sensor is guaranteed to get ID1.
     * Other sensor gets an ID > 5
     */
    
    switch(crestadev->sensor_type) {
	case CRESTA_SENSOR_TYPE_ANEMOMETER: {
	  if(++sensor_counts->anemometer_count == 1) {
	    device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_anemometer");
	  } else {
	    device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_anemometer", sensor_counts->anemometer_count + 1);
	  }
	  break;
	}
	case CRESTA_SENSOR_TYPE_UV: {
	  if(++sensor_counts->uv_count == 1) {
	     device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_uv");
	  } else {
	     device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_uv", sensor_counts->uv_count);
	  }
	  break;
	}
	case CRESTA_SENSOR_TYPE_RAIN: {
	  if(++sensor_counts->rain_count == 1) {
	     device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_rain");
	  } else {
	     device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_uv", sensor_counts->rain_count);
	  }
	  break;
	}
	case CRESTA_SENSOR_TYPE_THERMOHYGRO: {
	  //printk(KERN_INFO "sensor_addr = %x, sensor_addr & %x = %x\n", crestadev->sensor_addr, CRESTA_SENSOR_ADDR_MASK, crestadev->sensor_addr & CRESTA_SENSOR_ADDR_MASK);
	  if ((crestadev->sensor_addr & CRESTA_SENSOR_ADDR_MASK) == CRESTA_AM_THERMOHYGRO_CH5) {
	    if(++sensor_counts->thermohygro_ch5_count == 1) {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_thermohygro_ch5");
	    } else {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_thermohygro_ch5", sensor_counts->thermohygro_ch5_count);
	    }
	  } else if ((crestadev->sensor_addr & CRESTA_SENSOR_ADDR_MASK) == CRESTA_AM_THERMOHYGRO_CH4) {
	    if(++sensor_counts->thermohygro_ch4_count == 1) {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_thermohygro_ch4");
	    } else {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_thermohygro_ch4", sensor_counts->thermohygro_ch4_count);
	    }
	  } else if ((crestadev->sensor_addr & CRESTA_SENSOR_ADDR_MASK) == CRESTA_AM_THERMOHYGRO_CH3) {
	    if(++sensor_counts->thermohygro_ch3_count == 1) {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_thermohygro_ch3");
	    } else {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_thermohygro_ch3", sensor_counts->thermohygro_ch3_count);
	    }
	  } else if ((crestadev->sensor_addr & CRESTA_SENSOR_ADDR_MASK) == CRESTA_AM_THERMOHYGRO_CH2) {
	    if(++sensor_counts->thermohygro_ch2_count == 1) {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_thermohygro_ch2");
	    } else {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_thermohygro_ch2", sensor_counts->thermohygro_ch2_count);
	    }
	  } else if((crestadev->sensor_addr & CRESTA_SENSOR_ADDR_MASK) == CRESTA_AM_THERMOHYGRO_CH1) {
	    if(++sensor_counts->thermohygro_ch1_count == 1) {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s", "cresta_thermohygro_ch1");
	    } else {
	      device_create(cresta_class, NULL, crestadev->dev, NULL, "%s_%d", "cresta_thermohygro_ch1", sensor_counts->thermohygro_ch1_count);
	    }
	  }
	  break;
	}
	
      } //switch
    }
}

/*
 * Delete character device and remove entry in /dev
 */ 
void remove_device_entry(struct cresta_dev* crestadev) {
  
  device_destroy(cresta_class, crestadev->dev);
  cdev_del(&crestadev->cdev);
}
