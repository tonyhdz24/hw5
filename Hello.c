#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BSU CS 452 HW5");
MODULE_AUTHOR("<buff@cs.boisestate.edu>");

// One per driver (GLOBAL)
typedef struct
{
  dev_t devno;      // Device number assigned by kernel
  struct cdev cdev; // Character device Structure
  char *s;          // Global string ( Default separators)
} Device;           /* per-init() data */

// One per open() ( PER INSTANCE )
// TODO Add fields for seperators, data, position, mode flag
typedef struct
{
  char *s; // Copy of the string for this instance
} File;    /* per-open() data */

static Device device;

// TODO init new FILE fields copy default separators
static int open(struct inode *inode, struct file *filp)
{
  // 1. Allocate memory for instance data
  File *file = (File *)kmalloc(sizeof(*file), GFP_KERNEL);
  if (!file)
  {
    printk(KERN_ERR "%s: kmalloc() failed\n", DEVNAME);
    return -ENOMEM;
  }
  // 2. Allocate memory for this instance's string copy
  file->s = (char *)kmalloc(strlen(device.s) + 1, GFP_KERNEL);
  if (!file->s)
  {
    printk(KERN_ERR "%s: kmalloc() failed\n", DEVNAME);

    // Clean up first allocation
    kfree(file);
    return -ENOMEM;
  }
  // 3. Copy the global string to this instance
  strcpy(file->s, device.s);
  // 4. Store intance data where other functions can find it
  filp->private_data = file;

  // SUCCESS
  return 0;
}

// TODO Free any new fields added
static int release(struct inode *inode, struct file *filp)
{
  // Get intance data
  File *file = filp->private_data;
  // Free String
  kfree(file->s);

  // Free struct
  kfree(file);
  return 0;
}
// TODO Complete rewrite (Implement token scanning logic)
static ssize_t read(struct file *filp,
                    char *buf,
                    size_t count,
                    loff_t *f_pos)
{
  File *file = filp->private_data;
  // How many bytes to copy
  int n = strlen(file->s);
  n = (n < count ? n : count);

  // Copy from kernel space to user space
  if (copy_to_user(buf, file->s, n))
  {
    printk(KERN_ERR "%s: copy_to_user() failed\n", DEVNAME);
    return 0;
  }

  // Return number of bytes copied
  return n;
}
// TODO set next write is separators flag
static long ioctl(struct file *filp,
                  unsigned int cmd,
                  unsigned long arg)
{
  return 0;
}

static struct file_operations ops = {
    .open = open,
    .release = release,
    .read = read,
    .write = write,
    .unlocked_ioctl = ioctl,
    .owner = THIS_MODULE};

// TODO Init default separators instead of "Hello world!"
static int __init my_init(void)
{
  const char *s = "Hello world!\n";
  int err;
  // 1. Allocating and copy default string
  device.s = (char *)kmalloc(strlen(s) + 1, GFP_KERNEL);
  if (!device.s)
  {
    printk(KERN_ERR "%s: kmalloc() failed\n", DEVNAME);
    return -ENOMEM;
  }
  strcpy(device.s, s);

  // 2. Get a device number from the kernel
  err = alloc_chrdev_region(&device.devno, 0, 1, DEVNAME);
  if (err < 0)
  {
    printk(KERN_ERR "%s: alloc_chrdev_region() failed\n", DEVNAME);
    return err;
  }

  // 3. Initialize the character device with out operations
  cdev_init(&device.cdev, &ops);
  device.cdev.owner = THIS_MODULE;

  // 4. Add the device to the system
  err = cdev_add(&device.cdev, device.devno, 1);
  if (err)
  {
    printk(KERN_ERR "%s: cdev_add() failed\n", DEVNAME);
    return err;
  }
  printk(KERN_INFO "%s: init\n", DEVNAME);
  return 0;
}

static void __exit my_exit(void)
{
  cdev_del(&device.cdev);
  unregister_chrdev_region(device.devno, 1);
  kfree(device.s);
  printk(KERN_INFO "%s: exit\n", DEVNAME);
}

module_init(my_init);
module_exit(my_exit);
