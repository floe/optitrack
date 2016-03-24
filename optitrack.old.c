/* NaturalPoint Optitrack Driver 0.1

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.

  Copyright (C) 2008 by Florian 'Floe' Echtler <floe@butterbrot.org>

  Originally derived from the USB Skeleton driver 1.1,
  Copyright (C) 2003 Greg Kroah-Hartman (greg@kroah.com)

*/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

/* version information */
#define DRIVER_VERSION "0.1"
#define DRIVER_SHORT   "optitrack"
#define DRIVER_AUTHOR  "Florian 'Floe' Echtler <floe@butterbrot.org>"
#define DRIVER_DESC    "Optitrack Camera Driver"

/* minor number for misc USB devices */
#define USB_OPTI_MINOR_BASE 133

#define BUFSIZE 2048

/* device ID table */
static struct usb_device_id optitrack_table[] = {
	{USB_DEVICE(0x131D, 0x0125)}, /* NaturalPoint Optitrack */
	{}                            /* terminating null entry */
};

static char cmd_init[]  = { 0x14, 0x01 };
static char cmd_unkn1[] = { 0x10, 0x00, 0x80 };
static char cmd_unkn2[] = { 0x10, 0x00, 0x20 };
static char cmd_info[]  = { 0x17 };

#define optitrack_command(dev, buffer, sent) \
	usb_bulk_msg( dev->udev, usb_sndbulkpipe( dev->udev, 2 ), buffer, sizeof(buffer), &sent, HZ )

MODULE_DEVICE_TABLE(usb, optitrack_table);

/* structure to hold all of our device specific stuff */
struct usb_optitrack {

	struct usb_device *udev; /* save off the usb device pointer */
	struct usb_interface *interface; /* the interface for this device */

	unsigned char *bulk_in_buffer; /* the buffer to receive data */
	size_t bulk_in_size; /* the maximum bulk packet size */
	size_t bulk_out_size; /* the maximum bulk packet size */
	//size_t orig_bi_size; /* same as above, but reported by the device */
	__u8 bulk_in_endpointAddr; /* the address of the bulk in endpoint */
	__u8 bulk_out_endpointAddr; /* the address of the bulk in endpoint */

	int open; /* if the port is open or not */
	int present; /* if the device is not disconnected */
	struct semaphore sem; /* locks this structure */

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
static struct file_operations optitrack_fops = {
	.owner = THIS_MODULE,
	.read = optitrack_read,
	.open = optitrack_open,
	.release = optitrack_release,
};

/* class driver information for devfs */
static struct usb_class_driver optitrack_class = {
	.name = "usb/optitrack%d",
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

/* prevent races between open() and disconnect() */
static DECLARE_MUTEX(disconnect_sem);

static int optitrack_create_image(struct usb_optitrack *dev)
{
	int bytes_read = 0;
  int bytes_sent = 0;
	int bulk_read = 0;
	int result = 0;

  printk("create_image\n");

  result = usb_bulk_msg( 
		dev->udev, 
		usb_sndbulkpipe( dev->udev, dev->bulk_out_endpointAddr ), 
		&cmd_init, sizeof(cmd_init), &bytes_sent, HZ );

  printk("sent cmd_init\n");
	printk("result: %d, sent: %d\n",result,bytes_sent);
  if (result < 0) return result;

  result = usb_bulk_msg( 
		dev->udev, 
		usb_sndbulkpipe( dev->udev, dev->bulk_out_endpointAddr ), 
		&cmd_unkn1, sizeof(cmd_unkn1), &bytes_sent, HZ );
	
  printk("sent cmd_unkn1\n");
	printk("result: %d, sent: %d\n",result,bytes_sent);
	if (result < 0) return result;

  result = usb_bulk_msg( 
		dev->udev, 
		usb_sndbulkpipe( dev->udev, dev->bulk_out_endpointAddr ), 
		&cmd_unkn2, sizeof(cmd_unkn2), &bytes_sent, HZ );
	
  printk("sent cmd_unkn2\n");
	printk("result: %d, sent: %d\n",result,bytes_sent);
  if (result < 0) return result;

  result = usb_bulk_msg( 
		dev->udev, 
		usb_sndbulkpipe( dev->udev, dev->bulk_out_endpointAddr ), 
		&cmd_info, sizeof(cmd_info), &bytes_sent, HZ );
	
	printk("sent cmd_info\n");
	printk("result: %d, sent: %d\n",result,bytes_sent);
  if (result < 0) return result;

	/* loop over a blocking bulk read to get data from the device */
	while (bytes_read < BUFSIZE) {
		result = usb_bulk_msg (dev->udev,
				usb_rcvbulkpipe (dev->udev, dev->bulk_in_endpointAddr),
				dev->bulk_in_buffer + bytes_read,
				dev->bulk_in_size, &bulk_read, HZ * 5);
    if (result < 0) break;
		if (signal_pending(current)) {
			result = -EINTR;
			break;
		}
		bytes_read += bulk_read;
    if (bulk_read < dev->bulk_in_size) break;
	}

	dbg("read %d bytes info data", bytes_read); 
	return result;
}

static inline void optitrack_delete(struct usb_optitrack *dev)
{
	kfree(dev->bulk_in_buffer);
	kfree(dev);
}

static int optitrack_open(struct inode *inode, struct file *file)
{
	struct usb_optitrack *dev = NULL;
	struct usb_interface *interface;
	int result = 0;

	/* prevent disconnects */
	down(&disconnect_sem);

	/* get the interface from minor number and driver information */
	interface = usb_find_interface (&optitrack_driver, iminor (inode));
	if (!interface) {
		up(&disconnect_sem);
		return -ENODEV;
	}
	/* get the device information block from the interface */
	dev = usb_get_intfdata(interface);
	if (!dev) {
		up(&disconnect_sem);
		return -ENODEV;
	}

	/* lock this device */
	down(&dev->sem);

	/* check if already open */
	if (dev->open) {

		/* already open, so fail */
		result = -EBUSY;

	} else {

		/* create a new image and check for success */
		result = optitrack_create_image (dev);
		if (result)
			goto error;

		/* increment our usage count for the driver */
		++dev->open;

		/* save our object in the file's private structure */
		file->private_data = dev;

	} 

error:

	/* unlock this device */
	up(&dev->sem);

	/* unlock the disconnect semaphore */
	up(&disconnect_sem);
	return result;
}

static int optitrack_release(struct inode *inode, struct file *file)
{
	struct usb_optitrack *dev;

	/* prevent a race condition with open() */
	down(&disconnect_sem);

	dev = (struct usb_optitrack *) file->private_data;

	if (dev == NULL) {
		up(&disconnect_sem);
		return -ENODEV;
	}

	/* lock our device */
	down(&dev->sem);

	/* are we really open? */
	if (dev->open <= 0) {
		up(&dev->sem);
		up(&disconnect_sem);
		return -ENODEV;
	}

	--dev->open;

	if (!dev->present) {
		/* the device was unplugged before the file was released */
		up(&dev->sem);
		optitrack_delete(dev);
		up(&disconnect_sem);
		return 0;
	}

	up(&dev->sem);
	up(&disconnect_sem);
	return 0;
}

static ssize_t optitrack_read(struct file *file, char __user *buffer, size_t count,
				loff_t * ppos)
{
	struct usb_optitrack *dev;
	int result = 0;

	dev = (struct usb_optitrack *) file->private_data;

	/* lock this object */
	down (&dev->sem);

	/* verify that the device wasn't unplugged */
	if (!dev->present) {
		up (&dev->sem);
		return -ENODEV;
	}

	if (*ppos >= BUFSIZE) {
		up (&dev->sem);
		return 0;
	}

	count = min ((loff_t)count, BUFSIZE - (*ppos));

	if (copy_to_user (buffer, dev->bulk_in_buffer + *ppos, count)) {
		result = -EFAULT;
	} else {
		result = count;
		*ppos += count;
	}

	/* unlock the device */
	up(&dev->sem);
	return result;
}

static int optitrack_probe(struct usb_interface *interface,
				const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_optitrack *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *iendpoint;
	struct usb_endpoint_descriptor *oendpoint;
	int result;

	/* allocate memory for our device state and initialize it */
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;
	memset(dev, 0x00, sizeof(*dev));

	init_MUTEX(&dev->sem);
	dev->udev = udev;
	dev->interface = interface;

	/* set up the endpoint information - use only the first bulk-in and out endpoints */
  iface_desc = interface->cur_altsetting;
	oendpoint = &iface_desc->endpoint[0].desc;
	iendpoint = &iface_desc->endpoint[1].desc;

	if (!dev->bulk_in_endpointAddr
		&& ((iendpoint->bEndpointAddress & USB_DIR_IN) == USB_DIR_IN)
		&& ((iendpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		USB_ENDPOINT_XFER_BULK)) {

		/* we found a bulk in endpoint */
		dev->bulk_in_size = le16_to_cpu(iendpoint->wMaxPacketSize);
		dev->bulk_in_endpointAddr = iendpoint->bEndpointAddress;
		dev->bulk_in_buffer =
			kmalloc(BUFSIZE + dev->bulk_in_size, GFP_KERNEL);

		if (!dev->bulk_in_buffer) {
			err("Unable to allocate input buffer.");
			optitrack_delete(dev);
			return -ENOMEM;
		}
	}

	if (!(dev->bulk_out_endpointAddr)
		&& ((oendpoint->bEndpointAddress & USB_DIR_OUT) == USB_DIR_OUT)
		&& ((oendpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		USB_ENDPOINT_XFER_BULK)) {

		dev->bulk_out_endpointAddr = oendpoint->bEndpointAddress;
	}

	if (!(dev->bulk_in_endpointAddr)) {
		err("Unable to find bulk-in endpoint.");
		optitrack_delete(dev);
		return -ENODEV;
	}
	
	if (!(dev->bulk_out_endpointAddr)) {
		err("Unable to find bulk-out endpoint.");
		optitrack_delete(dev);
		return -ENODEV;
	}
	
	/* allow device read, write and ioctl */
	dev->present = 1;

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, dev);
	result = usb_register_dev(interface, &optitrack_class);

	/* something prevented us from registering this device */
	if (result) {
		err("Unable to allocate minor number.");
		usb_set_intfdata(interface, NULL);
		optitrack_delete(dev);
		return result;
	}

	/* be noisy */
	dev_info(&interface->dev,"%s now attached\n",DRIVER_DESC);

	return 0;
}

static void optitrack_disconnect(struct usb_interface *interface)
{
	struct usb_optitrack *dev;

	/* prevent races with open() */
	down(&disconnect_sem);

	/* get device structure */
	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* lock it */
	down(&dev->sem);

	/* give back our minor */
	usb_deregister_dev(interface, &optitrack_class);

	/* prevent device read, write and ioctl */
	dev->present = 0;

	/* unlock */
	up(&dev->sem);

	/* if the device is opened, optitrack_release will clean this up */
	if (!dev->open)
		optitrack_delete(dev);

	up(&disconnect_sem);

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

