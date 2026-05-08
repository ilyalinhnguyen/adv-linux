#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "int_stack_ioctl.h"

#define DEVICE_NAME "int_stack"
#define CLASS_NAME "int_stack_class"
#define DEFAULT_STACK_CAPACITY 16

struct int_stack_dev {
	int *data;
	size_t capacity;
	size_t top;
	struct rw_semaphore rwsem;
	atomic_t open_count;
	dev_t dev_num;
	struct cdev cdev;
	struct class *class;
	struct device *device;
};

static struct int_stack_dev g_stack = {
	.data = NULL,
	.capacity = 0,
	.top = 0,
};

static int int_stack_open(struct inode *inode, struct file *file)
{
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

static int __init int_stack_init(void)
{
	int ret;

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
		goto err_del_cdev;
	}

	g_stack.device = device_create(g_stack.class, NULL, g_stack.dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(g_stack.device)) {
		ret = PTR_ERR(g_stack.device);
		goto err_destroy_class;
	}

	pr_info("int_stack: loaded (major=%d, minor=%d)\n", MAJOR(g_stack.dev_num), MINOR(g_stack.dev_num));
	return 0;

err_destroy_class:
	class_destroy(g_stack.class);
err_del_cdev:
	cdev_del(&g_stack.cdev);
err_unregister_chrdev:
	unregister_chrdev_region(g_stack.dev_num, 1);
err_free_stack:
	kfree(g_stack.data);
	g_stack.data = NULL;
	return ret;
}

static void __exit int_stack_exit(void)
{
	device_destroy(g_stack.class, g_stack.dev_num);
	class_destroy(g_stack.class);
	cdev_del(&g_stack.cdev);
	unregister_chrdev_region(g_stack.dev_num, 1);
	kfree(g_stack.data);
	g_stack.data = NULL;
	g_stack.capacity = 0;
	g_stack.top = 0;
	pr_info("int_stack: unloaded\n");
}

module_init(int_stack_init);
module_exit(int_stack_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilya-Linh Nguyen");
MODULE_DESCRIPTION("Thread-safe integer stack char device");
