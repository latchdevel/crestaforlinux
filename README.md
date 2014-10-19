#Weather station sensor data receiver for Linux#

Many weather stations such as Cresta, Hideki, Honeywell, Irox, Mebus, and TFA Nexus devices use a common protocol to receive data from wireless 433MHz sensors. This project consists of a Linux kernel module for receiving and decoding the sensor data and a user space tool to display the received data.

The module was written for Linux kernel 3.12.28, which is shipped with the wheezy release of raspbian. By default, the kernel module expects a 433MHz receiver to be connected to GPIO 27 of a Raspberry PI.

### Quick start guide for raspberry pi ###
* Connect a 433 MHz receiver to GPIO pin 27 of a raspberry pi
* Get the kernel sources, compile & install them
* Compile & install the cresta kernel module
* Load kernel module with modprobe cresta
* Wait until sensors are discovered (see /var/log/messages for progress)
* Use user space tool to read sensor from /dev/cresta_<sensor>
