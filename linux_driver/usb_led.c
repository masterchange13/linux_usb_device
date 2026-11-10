// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#define LED_VID 0x303a
#define LED_PID 0x1001
#define LED_PRODUCT "ESP32-S3 LED Device"
#define LED_MINOR_BASE 192
#define LED_FRAME_SIZE 4
#define LED_TIMEOUT_MS 2000

struct usb_led {
	struct usb_device *udev;
	struct usb_interface *interface;
	struct mutex lock;
	struct kref kref;
	u8 bulk_in;
	u8 bulk_out;
	bool busy;
};

static struct usb_driver led_driver;

static void led_delete(struct kref *ref)
{
	struct usb_led *dev = container_of(ref, struct usb_led, kref);

	usb_put_dev(dev->udev);
	kfree(dev);
}

static int led_open(struct inode *inode, struct file *file)
{
	struct usb_interface *interface;
	struct usb_led *dev;
	int ret;

	interface = usb_find_interface(&led_driver, iminor(inode));
	if (!interface)
		return -ENODEV;
	dev = usb_get_intfdata(interface);
	if (!dev)
		return -ENODEV;
	kref_get(&dev->kref);
	mutex_lock(&dev->lock);
	if (!dev->interface)
		ret = -ENODEV;
	else if (dev->busy)
		ret = -EBUSY;
	else {
		dev->busy = true;
		file->private_data = dev;
		ret = 0;
	}
	mutex_unlock(&dev->lock);
	if (ret)
		kref_put(&dev->kref, led_delete);
	return ret;
}

static int led_release(struct inode *inode, struct file *file)
{
	struct usb_led *dev = file->private_data;

	(void)inode;
	mutex_lock(&dev->lock);
	dev->busy = false;
	mutex_unlock(&dev->lock);
	kref_put(&dev->kref, led_delete);
	return 0;
}

static ssize_t led_read(struct file *file, char __user *buffer, size_t count,
			loff_t *offset)
{
	struct usb_led *dev = file->private_data;
	u8 frame[LED_FRAME_SIZE];
	int actual, ret;

	(void)offset;
	if (count < LED_FRAME_SIZE)
		return -EINVAL;
	mutex_lock(&dev->lock);
	if (!dev->interface) {
		ret = -ENODEV;
		goto out;
	}
	ret = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, dev->bulk_in),
			   frame, sizeof(frame), &actual, LED_TIMEOUT_MS);
	if (ret)
		goto out;
	if (actual != LED_FRAME_SIZE) {
		ret = -EIO;
		goto out;
	}
	ret = copy_to_user(buffer, frame, sizeof(frame)) ? -EFAULT : actual;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t led_write(struct file *file, const char __user *buffer,
			 size_t count, loff_t *offset)
{
	struct usb_led *dev = file->private_data;
	u8 frame[LED_FRAME_SIZE];
	int actual, ret;

	(void)offset;
	if (count != LED_FRAME_SIZE)
		return -EINVAL;
	if (copy_from_user(frame, buffer, sizeof(frame)))
		return -EFAULT;
	mutex_lock(&dev->lock);
	if (!dev->interface) {
		ret = -ENODEV;
		goto out;
	}
	ret = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, dev->bulk_out),
			   frame, sizeof(frame), &actual, LED_TIMEOUT_MS);
	if (!ret)
		ret = actual == LED_FRAME_SIZE ? actual : -EIO;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static const struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.release = led_release,
	.read = led_read,
	.write = led_write,
	.llseek = no_llseek,
};

static struct usb_class_driver led_class = {
	.name = "usb_led%d",
	.fops = &led_fops,
	.minor_base = LED_MINOR_BASE,
};

static int led_probe(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *alt = interface->cur_altsetting;
	struct usb_led *dev;
	char product[64];
	int i, ret;

	(void)id;
	dev_info(&interface->dev,
		 "probe: interface %u, class 0x%02x, subclass 0x%02x, protocol 0x%02x, %u endpoints\n",
		 alt->desc.bInterfaceNumber, alt->desc.bInterfaceClass,
		 alt->desc.bInterfaceSubClass, alt->desc.bInterfaceProtocol,
		 alt->desc.bNumEndpoints);
	if (alt->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC)
		return -ENODEV;
	ret = usb_string(udev, udev->descriptor.iProduct,
			 product, sizeof(product));
	if (ret < 0) {
		dev_warn(&interface->dev, "cannot read USB product string: %d\n", ret);
		return -ENODEV;
	}
	if (strcmp(product, LED_PRODUCT)) {
		dev_warn(&interface->dev, "unexpected product string: %s\n", product);
		return -ENODEV;
	}
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->udev = usb_get_dev(udev);
	dev->interface = interface;
	mutex_init(&dev->lock);
	kref_init(&dev->kref);
	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep = &alt->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep))
			dev->bulk_in = ep->bEndpointAddress;
		else if (usb_endpoint_is_bulk_out(ep))
			dev->bulk_out = ep->bEndpointAddress;
	}
	if (!dev->bulk_in || !dev->bulk_out) {
		dev_warn(&interface->dev, "bulk IN/OUT endpoints missing\n");
		ret = -ENODEV;
		goto error;
	}
	usb_set_intfdata(interface, dev);
	ret = usb_register_dev(interface, &led_class);
	if (ret) {
		dev_err(&interface->dev, "usb_register_dev failed: %d\n", ret);
		usb_set_intfdata(interface, NULL);
		goto error;
	}
	dev_info(&interface->dev, "USB LED registered (USB major 180, minor %d; see /dev/usb_led*)\n",
		 interface->minor);
	return 0;
error:
	kref_put(&dev->kref, led_delete);
	return ret;
}

static void led_disconnect(struct usb_interface *interface)
{
	struct usb_led *dev = usb_get_intfdata(interface);

	usb_deregister_dev(interface, &led_class);
	mutex_lock(&dev->lock);
	dev->interface = NULL;
	usb_set_intfdata(interface, NULL);
	mutex_unlock(&dev->lock);
	kref_put(&dev->kref, led_delete);
}

static const struct usb_device_id led_ids[] = {
	{ USB_DEVICE(LED_VID, LED_PID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, led_ids);

static struct usb_driver led_driver = {
	.name = "usb_led",
	.probe = led_probe,
	.disconnect = led_disconnect,
	.id_table = led_ids,
};

static int __init led_init(void)
{
	int ret;

	pr_info("usb_led: loading USB driver for %04x:%04x\n", LED_VID, LED_PID);
	ret = usb_register(&led_driver);
	if (ret)
		pr_err("usb_led: usb_register failed: %d\n", ret);
	else
		pr_info("usb_led: USB driver registered\n");
	return ret;
}

static void __exit led_exit(void)
{
	usb_deregister(&led_driver);
	pr_info("usb_led: USB driver unregistered\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("ESP32-S3 LED USB character device");
