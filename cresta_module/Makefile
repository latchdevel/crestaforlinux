MODULE=cresta
 

cresta-objs += cresta_interrupthandler.o cresta_sensor_mgmt.o cresta_chardevice.o
obj-m += ${MODULE}.o
 
module_upload=${MODULE}.ko
 
all: clean compile
	
compile:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:

info:
	modinfo  ${module_upload}

