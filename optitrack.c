/* NaturalPoint Optitrack driver v0.1

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.

  Copyright (C) 2008 by Florian 'Floe' Echtler <floe@butterbrot.org>

  Originally derived from the USB Skeleton driver 1.1,
  Copyright (C) 2003 Greg Kroah-Hartman (greg@kroah.com)

*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

/* version information */
#define DRIVER_VERSION "0.1"
#define DRIVER_SHORT   "optitrack"
#define DRIVER_AUTHOR  "Florian 'Floe' Echtler <floe@butterbrot.org>"
#define DRIVER_DESC    "NaturalPoint Optitrack Camera Driver"

/* minor number for misc USB devices */
#define USB_OPTI_MINOR_BASE 133

/* vendor and device IDs */
#define ID_NATURALPOINT 0x131D
#define ID_OPTITRACK    0x0125

/* device ID table */
static struct usb_device_id optitrack_table[] = {
	{USB_DEVICE(ID_NATURALPOINT, ID_OPTITRACK)}, 
	{}
};

/* camera commands */
static char cmd_set_led[]    = { 0x10, 0x00, 0x00 };
static char cmd_start_cam[]  = { 0x12 };
static char cmd_stop_cam[]   = { 0x13 };
static char cmd_reset[]      = { 0x14, 0x01 };
static char cmd_set_thresh[] = { 0x15, 0x00, 0x01 };
static char cmd_get_info[]   = { 0x17 };

MODULE_DEVICE_TABLE(usb, optitrack_table);
static DEFINE_MUTEX(open_disc_mutex);

/* structure to hold all of our device specific stuff */
struct usb_optitrack {

	struct usb_device *udev; /* save off the usb device pointer */
	struct usb_interface *interface; /* the interface for this device */

	unsigned char *bulk_in_buffer; /* the buffer to receive data */
	size_t bulk_in_size; /* the maximum bulk packet size */
	size_t bulk_out_size; /* same as above, output endpoint */
	__u8 bulk_in_endpointAddr; /* the address of the bulk in endpoint */
	__u8 bulk_out_endpointAddr; /* the address of the bulk out endpoint */

	int open; /* if the port is open or not */
	int present; /* if the device is not disconnected */
	int serial; /* serial number of camera */
	int data_size; /* bytes from last bulk-in transfer */
	struct mutex lock; /* locks this structure */

};

/* local function prototypes */
static ssize_t optitrack_read(struct file *file, char __user *buffer,
				size_t count, loff_t * ppos);

static int optitrack_open(struct inode *inode, struct file *file);
static int optitrack_release(struct inode *inode, struct file *file);

static int optitrack_probe(struct usb_interface *interface,
				const struct usb_device_id *id);

static void optitrack_disconnect(struct usb_interface *interface);

/* file operation pointers */
static const struct file_operations optitrack_fops = {
	.owner = THIS_MODULE,
	.read = optitrack_read,
	.open = optitrack_open,
	.release = optitrack_release,
};

/* class driver information */
static struct usb_class_driver optitrack_class = {
	.name = "optitrack%d",
	.fops = &optitrack_fops,
	.minor_base = USB_OPTI_MINOR_BASE,
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver optitrack_driver = {
	.name = DRIVER_SHORT,
	.probe = optitrack_probe,
	.disconnect = optitrack_disconnect,
	.id_table = optitrack_table,
};

static void optitrack_bulk_cb(struct urb* urb) {
	printk("read callback: status %d actual_length %d\n",urb->status,urb->actual_length);
}

#define optitrack_command(dev, buffer, sent) \
	usb_bulk_msg( \
		dev->udev, \
		usb_sndbulkpipe( dev->udev, dev->bulk_out_endpointAddr ), \
		buffer, \
		sizeof(buffer), \
		&sent, \
		HZ \
	)

/*int optitrack_do_command(struct usb_optitrack *dev, unsigned char* buffer, int size, int* sent) {
	struct urb* mysendurb = usb_alloc_urb(0,GFP_KERNEL);
	int result = 0;
	printk("submitting urb..\n");
	usb_fill_bulk_urb( mysendurb, dev->udev, usb_sndbulkpipe( dev->udev, dev->bulk_out_endpointAddr ), buffer, size, optitrack_bulk_cb, dev );
	result = usb_submit_urb(mysendurb,GFP_KERNEL);
	printk("..done.\n");
	*sent = 0;
	usb_free_urb(mysendurb);
	return result;
}

#define optitrack_command(dev,buffer,sent) optitrack_do_command(dev,buffer,sizeof(buffer),&sent)*/

static int optitrack_init(struct usb_optitrack *dev)
{
	int bytes_read = 0;
	int bytes_written;
	int result = 0;

	struct urb* myrecvurb;
	if (optitrack_command(dev, cmd_reset, bytes_written) < 0)
		goto reset;

	cmd_set_led[1] = 0x00; cmd_set_led[2] = 0x80;
	if (optitrack_command(dev, cmd_set_led, bytes_written) < 0)
		goto reset;

	cmd_set_led[1] = 0x00; cmd_set_led[2] = 0x20;
	if (optitrack_command(dev, cmd_set_led, bytes_written) < 0)
		goto reset;

	if (optitrack_command(dev, cmd_get_info, bytes_written) < 0)
		goto reset;

	msleep(1000);
	result = usb_bulk_msg (dev->udev,
		usb_rcvbulkpipe (dev->udev, dev->bulk_in_endpointAddr),
		dev->bulk_in_buffer,
		dev->bulk_in_size, &bytes_read, HZ);

	/*msleep(1000);
	result = usb_bulk_msg (dev->udev,
		usb_rcvbulkpipe (dev->udev, dev->bulk_in_endpointAddr),
		dev->bulk_in_buffer,
		dev->bulk_in_size, &bytes_read, HZ);*/

	/*printk("submitting urb..\n");
	myrecvurb = usb_alloc_urb(0,GFP_KERNEL);
	usb_fill_bulk_urb( myrecvurb, dev->udev, usb_rcvbulkpipe( dev->udev, dev->bulk_in_endpointAddr ), dev->bulk_in_buffer, dev->bulk_in_size, optitrack_bulk_cb, dev );
	result = usb_submit_urb(myrecvurb,GFP_KERNEL);
	usb_free_urb(myrecvurb);
	printk("..done.\n");

	msleep(1000);

	printk("submitting urb..\n");
	myrecvurb = usb_alloc_urb(0,GFP_KERNEL);
	usb_fill_bulk_urb( myrecvurb, dev->udev, usb_rcvbulkpipe( dev->udev, dev->bulk_in_endpointAddr ), dev->bulk_in_buffer, dev->bulk_in_size, optitrack_bulk_cb, dev );
	result = usb_submit_urb(myrecvurb,GFP_KERNEL);
	usb_free_urb(myrecvurb);
	printk("..done.\n");*/

	msleep(1000);

	if (!result && (bytes_read == 9)) {
		dev->serial = 
			(dev->bulk_in_buffer[5] << 8) +
			 dev->bulk_in_buffer[6];
	} else {
		dev->serial = -bytes_read;
	}

	cmd_set_led[1] = cmd_set_led[2] = 0xF0;
	if (optitrack_command(dev, cmd_set_led, bytes_written) < 0)
		goto reset;

	cmd_reset[1] = 0x00;
	if (optitrack_command(dev, cmd_reset, bytes_written) < 0)
		goto reset;

	if (optitrack_command(dev, cmd_start_cam, bytes_written) < 0)
		goto reset;

	if (optitrack_command(dev, cmd_stop_cam, bytes_written) < 0)
		goto reset;

	cmd_set_led[1] = 0x00;
	if (optitrack_command(dev, cmd_set_led, bytes_written) < 0)
		goto reset;

	/* reset the device */
reset:

	return result;
}

static inline void optitrack_delete(struct usb_optitrack *dev)
{
	kfree(dev->bulk_in_buffer);
	kfree(dev);
}

static int optitrack_open(struct inode *inode, struct file *file)
{
	struct usb_optitrack *dev;
	struct usb_interface *interface;
	int result = 0;

	/* get the interface from minor number and driver information */
	interface = usb_find_interface (&optitrack_driver, iminor (inode));
	if (!interface)
		return -ENODEV;

	mutex_lock(&open_disc_mutex);
	/* get the device information block from the interface */
	dev = usb_get_intfdata(interface);
	if (!dev) {
		mutex_unlock(&open_disc_mutex);
		return -ENODEV;
	}

	/* lock this device */
	mutex_lock(&dev->lock);
	mutex_unlock(&open_disc_mutex);

	/* check if already open */
	if (dev->open) {
		/* already open, so fail */
		result = -EBUSY;
	} else {
		/* increment our usage count for the driver */
		++dev->open;
		/* save our object in the file's private structure */
		file->private_data = dev;
	} 

	/* unlock this device */
	mutex_unlock(&dev->lock);
	return result;
}

static int optitrack_release(struct inode *inode, struct file *file)
{
	struct usb_optitrack *dev;

	dev = file->private_data;

	if (dev == NULL)
		return -ENODEV;

	mutex_lock(&open_disc_mutex);
	/* lock our device */
	mutex_lock(&dev->lock);

	/* are we really open? */
	if (dev->open <= 0) {
		mutex_unlock(&dev->lock);
		mutex_unlock(&open_disc_mutex);
		return -ENODEV;
	}

	--dev->open;

	if (!dev->present) {
		/* the device was unplugged before the file was released */
		mutex_unlock(&dev->lock);
		mutex_unlock(&open_disc_mutex);
		optitrack_delete(dev);
	} else {
		mutex_unlock(&dev->lock);
		mutex_unlock(&open_disc_mutex);
	}
	return 0;
}

static ssize_t optitrack_read(struct file *file, char __user *buffer, size_t count,
				loff_t * ppos)
{
	struct usb_optitrack *dev = file->private_data;
	int result;

	/* lock this object */
	mutex_lock(&dev->lock);

	/* verify that the device wasn't unplugged */
	if (!dev->present) {
		mutex_unlock(&dev->lock);
		return -ENODEV;
	}

	result = simple_read_from_buffer(buffer, count, ppos,
					dev->bulk_in_buffer, dev->data_size );
	/* unlock the device */
	mutex_unlock(&dev->lock);
	return result;
}

static int optitrack_probe(struct usb_interface *interface,
				const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_optitrack *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int result;

	/* check if we really have the right interface */
	iface_desc = &interface->altsetting[0];
	if (iface_desc->desc.bInterfaceClass != 0xFF)
		return -ENODEV;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	mutex_init(&dev->lock);
	dev->udev = udev;
	dev->interface = interface;

	/* set up the endpoint information - first bulk-in endpoint */
	endpoint = &iface_desc->endpoint[0].desc;
	if (!dev->bulk_out_endpointAddr && usb_endpoint_is_bulk_out(endpoint)) {
		/* we found a bulk in endpoint */
		dev->bulk_out_size = le16_to_cpu(endpoint->wMaxPacketSize);
		dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
	}

	/* set up the endpoint information - first bulk-in endpoint */
	endpoint = &iface_desc->endpoint[1].desc;
	if (!dev->bulk_in_endpointAddr && usb_endpoint_is_bulk_in(endpoint)) {
		/* we found a bulk in endpoint */
		dev->bulk_in_size = 4096; //le16_to_cpu(endpoint->wMaxPacketSize);
		dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
		dev->bulk_in_buffer =
			kmalloc(dev->bulk_in_size, GFP_KERNEL);

		if (!dev->bulk_in_buffer) {
			err("Unable to allocate input buffer.");
			optitrack_delete(dev);
			return -ENOMEM;
		}
	}

	if (!dev->bulk_in_endpointAddr || !dev->bulk_out_endpointAddr) {
		err("Unable to find both endpoints.");
		optitrack_delete(dev);
		return -ENODEV;
	}

	dev->bulk_in_endpointAddr = dev->bulk_in_endpointAddr & USB_ENDPOINT_NUMBER_MASK;
	printk("input endpoint: 0x%hx, output endpoint: 0x%hx\n",dev->bulk_in_endpointAddr,dev->bulk_out_endpointAddr);

	/* allow device read, write and ioctl */
	dev->present = 1;

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, dev);
	result = usb_register_dev(interface, &optitrack_class);
	if (result) {
		/* something prevented us from registering this device */
		err("Unble to allocate minor number.");
		usb_set_intfdata(interface, NULL);
		optitrack_delete(dev);
		return result;
	}

	optitrack_init(dev);

	/* be noisy */
	dev_info(&interface->dev,"%s (serial %d) now attached\n",DRIVER_DESC,dev->serial);

	return 0;
}

static void optitrack_disconnect(struct usb_interface *interface)
{
	struct usb_optitrack *dev;

	/* get device structure */
	dev = usb_get_intfdata(interface);

	/* give back our minor */
	usb_deregister_dev(interface, &optitrack_class);

	mutex_lock(&open_disc_mutex);
	usb_set_intfdata(interface, NULL);
	/* lock the device */
	mutex_lock(&dev->lock);
	mutex_unlock(&open_disc_mutex);

	/* prevent device read, write and ioctl */
	dev->present = 0;

	/* if the device is opened, optitrack_release will clean this up */
	if (!dev->open) {
		mutex_unlock(&dev->lock);
		optitrack_delete(dev);
	} else {
		/* unlock */
		mutex_unlock(&dev->lock);
	}

	info("%s disconnected", DRIVER_DESC);
}

static int __init usb_optitrack_init(void)
{
	int result;

	info(DRIVER_DESC " " DRIVER_VERSION);

	/* register this driver with the USB subsystem */
	result = usb_register(&optitrack_driver);
	if (result)
		err("Unable to register device (error %d).", result);

	return result;
}

static void __exit usb_optitrack_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&optitrack_driver);
}

module_init(usb_optitrack_init);
module_exit(usb_optitrack_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

