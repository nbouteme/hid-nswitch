HID-NSWITCH
===========

Kernel-mode Nintendo© Switch© peripherals driver.

Todo:

	- Expose Gyro/Accelerometer
	- Support Pro controllers
	- Exposes rom/ram/spi into char devices
	- Expose user space API
	- Handle HOME Led
	- Expose temperature sensor 
	- Support Right JC IR CAM

Working:

	- Joycons as individual event sources
	- Battery indicator
	- Individual player led control from sys files (not exposed in uinput)
	
Needs testing:

	- Rewrite command exchange to work be callable from worker thread
	- Support merged joycons as a single controller

