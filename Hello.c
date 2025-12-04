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
  struct cdev cdev; // Character device structure
  char *s;          // Global string (Default separators)
  size_t s_len;     // Length of default separators
} Device;

// One per open() (PER INSTANCE)
typedef struct
{
  char *separators;      // Separator characters
  size_t sep_len;        // Number of separator characters
  char *data;            // Data to scan
  size_t data_len;       // Length of data
  size_t current_pos;    // Current position in data
  size_t token_end;      // End position of current token
  int in_token;          // Flag: currently reading a token
  int next_write_is_sep; // Flag: next write sets separators
} File;

static Device device;

// Helper: check if character is a separator
static int is_sep(File *file, char c)
{
  size_t i;
  for (i = 0; i < file->sep_len; i++)
  {
    if (c == file->separators[i])
      return 1;
  }
  return 0;
}

static int open(struct inode *inode, struct file *filp)
{
  File *file = (File *)kmalloc(sizeof(*file), GFP_KERNEL);
  if (!file)
  {
    printk(KERN_ERR "%s: kmalloc() failed\n", DEVNAME);
    return -ENOMEM;
  }

  // Copy default separators
  file->sep_len = device.s_len;
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
  file->token_end = 0;
  file->in_token = 0;
  file->next_write_is_sep = 0;

  filp->private_data = file;
  return 0;
}

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
    // Writing data to scan - each write is a NEW sequence
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
    file->current_pos = 0;
    file->token_end = 0;
    file->in_token = 0;
  }

  return count;
}

/*
 * read() - Return next token (or portion of token)
 *
 * Returns:
 *   > 0  : Number of bytes of token copied
 *   0    : End of current token (call again for next token)
 *   -1   : End of data (no more tokens)
 */
static ssize_t read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
  File *file = filp->private_data;
  size_t bytes_to_copy;

  // No data to scan
  if (!file->data || file->data_len == 0)
    return -1;

  // If not currently in a token, find the next one
  if (!file->in_token)
  {
    // Skip leading separators
    while (file->current_pos < file->data_len && is_sep(file, file->data[file->current_pos]))
    {
      file->current_pos++;
    }

    // If we've reached the end, no more tokens
    if (file->current_pos >= file->data_len)
      return -1;

    // Find the end of this token
    file->token_end = file->current_pos;
    while (file->token_end < file->data_len && !is_sep(file, file->data[file->token_end]))
    {
      file->token_end++;
    }

    file->in_token = 1;
  }

  // We're in a token - return as much as we can
  bytes_to_copy = file->token_end - file->current_pos;

  if (bytes_to_copy == 0)
  {
    // We've returned the entire token, signal end of token
    file->in_token = 0;
    return 0;
  }

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

static struct file_operations ops = {
    .open = open,
    .release = release,
    .read = read,
    .write = write,
    .unlocked_ioctl = ioctl,
    .owner = THIS_MODULE};

static int __init my_init(void)
{
  const char *s = " \t\n"; // Default separators: space, tab, newline
  int err;

  // Allocate and copy default separators (NOT null-terminated)
  device.s_len = 3; // space, tab, newline
  device.s = (char *)kmalloc(device.s_len, GFP_KERNEL);
  if (!device.s)
  {
    printk(KERN_ERR "%s: kmalloc() failed\n", DEVNAME);
    return -ENOMEM;
  }
  memcpy(device.s, s, device.s_len);

  // Get a device number from the kernel
  err = alloc_chrdev_region(&device.devno, 0, 1, DEVNAME);
  if (err < 0)
  {
    printk(KERN_ERR "%s: alloc_chrdev_region() failed\n", DEVNAME);
    kfree(device.s);
    return err;
  }

  // Initialize the character device with our operations
  cdev_init(&device.cdev, &ops);
  device.cdev.owner = THIS_MODULE;

  // Add the device to the system
  err = cdev_add(&device.cdev, device.devno, 1);
  if (err)
  {
    printk(KERN_ERR "%s: cdev_add() failed\n", DEVNAME);
    unregister_chrdev_region(device.devno, 1);
    kfree(device.s);
    return err;
  }

  printk(KERN_INFO "%s: init (major=%d)\n", DEVNAME, MAJOR(device.devno));
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