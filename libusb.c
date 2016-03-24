#include <usb.h>
#include <unistd.h>
#include <stdio.h>
			
struct usb_device* getDevice() {

	struct usb_bus *busses;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();
		
	struct usb_bus *bus;
	//int c, i, a;

	struct usb_device *dev = 0;
	for (bus = busses; bus; bus = bus->next) {

		for (dev = bus->devices; dev; dev = dev->next) {
			/* Check if this device is a printer */
			if ((dev->descriptor.idVendor == 0x131d) && (dev->descriptor.idProduct == 0x0125)) {
				/* Open the device, claim the interface and do your processing */
				return dev;
			}

			/* Loop through all of the configurations 
			for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
				// Loop through all of the interfaces 
				for (i = 0; i < dev->config[c].bNumInterfaces; i++) {
					// Loop through all of the alternate settings 
					for (a = 0; a < dev->config[c].interface[i].num_altsetting; a++) {
						// Check if this interface is a printer 
						if (dev->config[c].interface[i].altsetting[a].bInterfaceClass == 7) {
							// Open the device, set the alternate setting, claim the interface and do your processing 
							...
						}
					}
				}
			}*/

		}
	}
	return 0;
}

static char cmd_set_led[]    = { 0x10, 0x00, 0x00 };
static char cmd_start_cam[]  = { 0x12 };
static char cmd_stop_cam[]   = { 0x13 };
static char cmd_reset[]      = { 0x14, 0x01 };
static char cmd_set_thresh[] = { 0x15, 0x00, 0x01 };
static char cmd_get_info[]   = { 0x17 };


int main() {

	char buffer[4096];
	int result = 0;

	struct usb_device* dev = getDevice();   if (!dev) return 1;
	usb_dev_handle* handle = usb_open(dev); if (!handle) return 2;
	
	if (usb_claim_interface(handle,0) < 0) return 3;

	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	usb_bulk_write(handle,2,cmd_reset,sizeof(cmd_reset),200);

	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	//cmd_set_led[2] = 0x80; usb_bulk_write(handle,2,cmd_set_led,sizeof(cmd_set_led),200);
	usb_bulk_write(handle,2,cmd_get_info,sizeof(cmd_get_info),200);

	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");
	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	cmd_set_led[1] = cmd_set_led[2] = 0xF0; usb_bulk_write(handle,2,cmd_set_led,sizeof(cmd_set_led),200);
	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");


	cmd_reset[1] = 0x00;
	usb_bulk_write(handle,2,cmd_reset,sizeof(cmd_reset),200);
	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	usb_bulk_write(handle,2,cmd_start_cam,sizeof(cmd_start_cam),200);

	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	/*while (result >= 0) {
		result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
		write(1,buffer,result);
		fflush(0);
	}*/
	sleep(1);

	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	usb_bulk_write(handle,2,cmd_stop_cam,sizeof(cmd_stop_cam),200);
	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	cmd_set_led[1] = 0x00; usb_bulk_write(handle,2,cmd_set_led,sizeof(cmd_set_led),200);
	result = usb_bulk_read(handle,2,buffer,sizeof(buffer),200);
	for (int i = 0; i < result; i++) printf("%hhx ",buffer[i]); printf("\n");

	usb_close(handle);
}

