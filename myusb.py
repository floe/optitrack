#!/usr/bin/python
import os 
import sys
import usb
import time
import struct
import array
import math

VENDOR_ID = 0x131d
PRODUCT_ID = 0x0125
INTERFACE_ID = 0
BULK_IN_EP = 2
BULK_OUT_EP = 2

def getDevice():
	buses = usb.busses()
	for bus in buses :
		for device in bus.devices :
			if device.idVendor == VENDOR_ID:
				if device.idProduct == PRODUCT_ID:
					print "Found: " + str(device)
					return device
	return None

def close(handle):
	handle.releaseInterface()

def sendMessage(handle,msg):
	handle.bulkWrite(BULK_OUT_EP,msg,200)

def getMessage(handle,size):
	# Gah! Returns a tuple of longs.  Why doesn't it return a string?
	return handle.bulkRead(BULK_IN_EP, size, 200 )

def main(argv=None):
	print "optitrack hack 0.01"
	dev = getDevice()
	handle = dev.open()
	handle.claimInterface(INTERFACE_ID)

	time.sleep(1)

	sendMessage(handle,chr(0x14)+chr(0x01)) # reset, maybe?

	sendMessage(handle,chr(0x10)+chr(0x00)+chr(0x80)) # turn leds off
	sendMessage(handle,chr(0x10)+chr(0x00)+chr(0x20))

	sendMessage(handle,chr(0x17)) # maybe get serial command?

	time.sleep(1)
	#print getMessage(handle,4096) # two reads only once after powerup, first one returns 6 bytes
	time.sleep(1)
	print getMessage(handle,4096) # this one always returns 9 bytes including the serial number

	time.sleep(1)

	sendMessage(handle,chr(0x10)+chr(0x10)+chr(0x10)) # enable led 1
	sendMessage(handle,chr(0x10)+chr(0x20)+chr(0x20)) # enable led 2
	sendMessage(handle,chr(0x10)+chr(0x40)+chr(0x40)) # enable led 3
	sendMessage(handle,chr(0x10)+chr(0x80)+chr(0x80)) # enable led 4

	time.sleep(1)

	sendMessage(handle,chr(0x14)+chr(0x00)) # reset? wipe sensor?

	sendMessage(handle,chr(0x12)) # start command (presumably)

	i = 0
	while i < 2:
		i = i+1
		foo = getMessage(handle,4096)
		str = ""
		for s in foo:
			str = str + chr(s)
		os.write(1,str)

	sendMessage(handle,chr(0x13)) # stop command (presumably)

main()

