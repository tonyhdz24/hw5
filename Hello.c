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
// Add fields for seperators, data, position, mode flag
typedef struct
{
  char *separators;
  size_t sep_len;
  char *data;
  size_t data_len;
  size_t current_pos;
  int next_write_is_sep;
} File;

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
  // Copy default separators
  file->sep_len = strlen(device.s);
  file->separators = (char *)kmalloc(file->sep_len, GFP_KERNEL);
  if (!file->separators)
  {
    kfree(file);
    return -ENOMEM;
  }
  memcpy(file->separators, device.s, file->sep_len);
  // Initialize other fields
  file->data = NULL;
  file->data_len = 0;
  file->current_pos = 0;
  file->next_write_is_sep = 0;

  filp->private_data = file;
  return 0;

  // // 2. Allocate memory for this instance's string copy
  // file->s = (char *)kmalloc(strlen(device.s) + 1, GFP_KERNEL);
  // if (!file->s)
  // {
  //   printk(KERN_ERR "%s: kmalloc() failed\n", DEVNAME);

  //   // Clean up first allocation
  //   kfree(file);
  //   return -ENOMEM;
  // }
  // // 3. Copy the global string to this instance
  // strcpy(file->s, device.s);
  // // 4. Store intance data where other functions can find it
  // filp->private_data = file;

  // // SUCCESS
  // return 0;
}

// TODO Free any new fields added
static int release(struct inode *inode, struct file *filp)
{
  File *file = filp->private_data;
  if (file->separators)
    kfree(file->separators);
  if (file->data)
    kfree(file->data);
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
static long ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
  File *file = filp->private_data;
  if (cmd == 0)
  {
    file->next_write_is_sep = 1;
    return 0;
  }
  return -EINVAL;
}

static ssize_t write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
  File *file = filp->private_data;
  char *temp;

  if (file->next_write_is_sep)
  {
    // Writing separators
    temp = (char *)kmalloc(count, GFP_KERNEL);
    if (!temp)
      return -ENOMEM;

    if (copy_from_user(temp, buf, count))
    {
      kfree(temp);
      return -EFAULT;
    }

    if (file->separators)
      kfree(file->separators);
    file->separators = temp;
    file->sep_len = count;
    file->next_write_is_sep = 0;
  }
  else
  {
    // Writing data to scan
    temp = (char *)kmalloc(count, GFP_KERNEL);
    if (!temp)
      return -ENOMEM;

    if (copy_from_user(temp, buf, count))
    {
      kfree(temp);
      return -EFAULT;
    }

    if (file->data)
      kfree(file->data);
    file->data = temp;
    file->data_len = count;
    file->current_pos = 0; // Reset to start
  }

  return count;
}

static struct file_operations ops = {
    .open = open,
    .release = release,
    .read = read,
    .write = write,
    .unlocked_ioctl = ioctl,
    .owner = THIS_MODULE};

static ssize_t read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
  File *file = filp->private_data;
  size_t i, token_start, token_end, bytes_to_copy;
  int is_separator;

  // No data to scan
  if (!file->data || file->data_len == 0)
    return -1;

  // Already at end of data
  if (file->current_pos >= file->data_len)
    return -1;

  // Skip leading separators to find start of token
  while (file->current_pos < file->data_len)
  {
    is_separator = 0;
    for (i = 0; i < file->sep_len; i++)
    {
      if (file->data[file->current_pos] == file->separators[i])
      {
        is_separator = 1;
        break;
      }
    }
    if (!is_separator)
      break; // Found start of token
    file->current_pos++;
  }

  // If we skipped to the end, no more tokens
  if (file->current_pos >= file->data_len)
    return -1;

  // Now we're at the start of a token
  token_start = file->current_pos;

  // Find the end of the token (next separator or end of data)
  token_end = token_start;
  while (token_end < file->data_len)
  {
    is_separator = 0;
    for (i = 0; i < file->sep_len; i++)
    {
      if (file->data[token_end] == file->separators[i])
      {
        is_separator = 1;
        break;
      }
    }
    if (is_separator)
      break; // Found end of token
    token_end++;
  }

  // Calculate how much of the token to return
  bytes_to_copy = token_end - file->current_pos;

  // If we're at the end of the token, return 0
  if (bytes_to_copy == 0)
    return 0;

  if (bytes_to_copy > count)
    bytes_to_copy = count;

  // Copy to user space
  if (copy_to_user(buf, &file->data[file->current_pos], bytes_to_copy))
  {
    printk(KERN_ERR "%s: copy_to_user() failed\n", DEVNAME);
    return -EFAULT;
  }

  file->current_pos += bytes_to_copy;

  return bytes_to_copy;
}
// TODO Init default separators instead of "Hello world!"
static int __init my_init(void)
{
  const char *s = " \t\n"; // Default separators: space, tab, newline
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
