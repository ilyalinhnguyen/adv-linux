#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "int_stack_ioctl.h"

#define DEVICE_NAME "int_stack"
#define CLASS_NAME "int_stack_class"
#define DEFAULT_STACK_CAPACITY 16

#define USB_KEY_VID 0x0951
#define USB_KEY_PID 0x1666

struct int_stack_dev {
	int *data;
	size_t capacity;
	size_t top;
	struct rw_semaphore rwsem;

	dev_t dev_num;
	struct cdev cdev;
	struct class *class;
	struct device *device; /* created only when key present */

	atomic_t open_count;
	bool key_present;
	struct mutex key_lock; /* protects key_present + device create/destroy */

	struct usb_device *key_udev; /* referenced while key is present */
};

static struct int_stack_dev g_stack = {
	.data = NULL,
	.capacity = 0,
	.top = 0,
	.device = NULL,
	.key_present = false,
	.key_udev = NULL,
};

static bool usb_key_matches(const struct usb_device *udev)
{
	u16 vid = le16_to_cpu(udev->descriptor.idVendor);
	u16 pid = le16_to_cpu(udev->descriptor.idProduct);

	return vid == USB_KEY_VID && pid == USB_KEY_PID;
}

static int int_stack_open(struct inode *inode, struct file *file)
{
	if (!READ_ONCE(g_stack.key_present))
		return -ENODEV;

	atomic_inc(&g_stack.open_count);
	file->private_data = &g_stack;
	return 0;
}

static int int_stack_release(struct inode *inode, struct file *file)
{
	atomic_dec(&g_stack.open_count);
	return 0;
}

static ssize_t int_stack_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int value;

	if (count < sizeof(value))
		return -EINVAL;

	down_write(&g_stack.rwsem);
	if (g_stack.top == 0) {
		up_write(&g_stack.rwsem);
		return 0;
	}
	value = g_stack.data[--g_stack.top];
	up_write(&g_stack.rwsem);

	if (copy_to_user(buf, &value, sizeof(value)))
		return -EFAULT;

	return sizeof(value);
}

static ssize_t int_stack_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int value;

	if (count < sizeof(value))
		return -EINVAL;

	if (copy_from_user(&value, buf, sizeof(value)))
		return -EFAULT;

	down_write(&g_stack.rwsem);
	if (g_stack.top >= g_stack.capacity) {
		up_write(&g_stack.rwsem);
		return -ERANGE;
	}
	g_stack.data[g_stack.top++] = value;
	up_write(&g_stack.rwsem);

	return sizeof(value);
}

static long int_stack_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int new_capacity;
	int *new_data;

	if (_IOC_TYPE(cmd) != INT_STACK_IOCTL_MAGIC)
		return -ENOTTY;

	if (cmd != INT_STACK_IOCTL_SET_SIZE)
		return -ENOTTY;

	if (copy_from_user(&new_capacity, (int __user *)arg, sizeof(new_capacity)))
		return -EFAULT;

	if (new_capacity <= 0)
		return -EINVAL;

	new_data = kcalloc((size_t)new_capacity, sizeof(*new_data), GFP_KERNEL);
	if (!new_data)
		return -ENOMEM;

	down_write(&g_stack.rwsem);
	kfree(g_stack.data);
	g_stack.data = new_data;
	g_stack.capacity = (size_t)new_capacity;
	g_stack.top = 0;
	up_write(&g_stack.rwsem);

	return 0;
}

static const struct file_operations int_stack_fops = {
	.owner = THIS_MODULE,
	.open = int_stack_open,
	.release = int_stack_release,
	.read = int_stack_read,
	.write = int_stack_write,
	.unlocked_ioctl = int_stack_ioctl,
};

static int key_create_chardev_node(void)
{
	if (g_stack.device)
		return 0;

	g_stack.device = device_create(g_stack.class, NULL, g_stack.dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(g_stack.device)) {
		int ret = PTR_ERR(g_stack.device);
		g_stack.device = NULL;
		return ret;
	}
	return 0;
}

static void key_destroy_chardev_node(void)
{
	if (!g_stack.device)
		return;
	device_destroy(g_stack.class, g_stack.dev_num);
	g_stack.device = NULL;
}

static void key_set_present(struct usb_device *udev)
{
	int ret;

	if (!usb_key_matches(udev))
		return;

	mutex_lock(&g_stack.key_lock);
	if (g_stack.key_present)
		goto out;

	ret = key_create_chardev_node();
	if (ret)
		goto out;

	g_stack.key_present = true;
	g_stack.key_udev = usb_get_dev(udev);
	pr_info("usb_int_stack: key inserted (vid=%04x pid=%04x) -> /dev/%s available\n",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		DEVICE_NAME);
out:
	mutex_unlock(&g_stack.key_lock);
}

static void key_set_absent(struct usb_device *udev)
{
	mutex_lock(&g_stack.key_lock);
	if (!g_stack.key_present || !g_stack.key_udev || g_stack.key_udev != udev) {
		mutex_unlock(&g_stack.key_lock);
		return;
	}

	g_stack.key_present = false;
	key_destroy_chardev_node();
	usb_put_dev(g_stack.key_udev);
	g_stack.key_udev = NULL;
	mutex_unlock(&g_stack.key_lock);

	pr_info("usb_int_stack: key removed -> /dev/%s removed (stack preserved)\n", DEVICE_NAME);
}

static int usb_key_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	struct usb_device *udev = data;

	if (!udev)
		return NOTIFY_DONE;

	switch (action) {
	case USB_DEVICE_ADD:
		key_set_present(udev);
		break;
	case USB_DEVICE_REMOVE:
		key_set_absent(udev);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block usb_nb = {
	.notifier_call = usb_key_notify,
};

static int find_existing_key(struct usb_device *udev, void *data)
{
	key_set_present(udev);
	return READ_ONCE(g_stack.key_present) ? 1 : 0;
}

static int __init usb_int_stack_init(void)
{
	int ret;

	mutex_init(&g_stack.key_lock);
	init_rwsem(&g_stack.rwsem);
	atomic_set(&g_stack.open_count, 0);

	g_stack.data = kcalloc(DEFAULT_STACK_CAPACITY, sizeof(*g_stack.data), GFP_KERNEL);
	if (!g_stack.data)
		return -ENOMEM;
	g_stack.capacity = DEFAULT_STACK_CAPACITY;
	g_stack.top = 0;

	ret = alloc_chrdev_region(&g_stack.dev_num, 0, 1, DEVICE_NAME);
	if (ret)
		goto err_free_stack;

	cdev_init(&g_stack.cdev, &int_stack_fops);
	g_stack.cdev.owner = THIS_MODULE;
	ret = cdev_add(&g_stack.cdev, g_stack.dev_num, 1);
	if (ret)
		goto err_unregister_chrdev;

	g_stack.class = class_create(CLASS_NAME);
	if (IS_ERR(g_stack.class)) {
		ret = PTR_ERR(g_stack.class);
		g_stack.class = NULL;
		goto err_del_cdev;
	}

	usb_register_notify(&usb_nb);
	usb_for_each_dev(NULL, find_existing_key);

	pr_info("usb_int_stack: loaded (major=%d, minor=%d). Waiting for USB key...\n",
		MAJOR(g_stack.dev_num), MINOR(g_stack.dev_num));
	return 0;

err_del_cdev:
	cdev_del(&g_stack.cdev);
err_unregister_chrdev:
	unregister_chrdev_region(g_stack.dev_num, 1);
err_free_stack:
	kfree(g_stack.data);
	g_stack.data = NULL;
	g_stack.capacity = 0;
	g_stack.top = 0;
	return ret;
}

static void __exit usb_int_stack_exit(void)
{
	usb_unregister_notify(&usb_nb);

	mutex_lock(&g_stack.key_lock);
	g_stack.key_present = false;
	key_destroy_chardev_node();
	if (g_stack.key_udev) {
		usb_put_dev(g_stack.key_udev);
		g_stack.key_udev = NULL;
	}
	mutex_unlock(&g_stack.key_lock);

	if (g_stack.class)
		class_destroy(g_stack.class);
	cdev_del(&g_stack.cdev);
	unregister_chrdev_region(g_stack.dev_num, 1);
	kfree(g_stack.data);
	g_stack.data = NULL;
	g_stack.capacity = 0;
	g_stack.top = 0;
	pr_info("usb_int_stack: unloaded\n");
}

module_init(usb_int_stack_init);
module_exit(usb_int_stack_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilya-Linh Nguyen");
MODULE_DESCRIPTION("Integer stack chardev gated by a USB electronic key");

