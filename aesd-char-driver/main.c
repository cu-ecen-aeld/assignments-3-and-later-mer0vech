/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/uaccess.h>
#include <linux/slab.h> 
#include "aesdchar.h"

// #define MAX_LINE_SUPPORTED 4096

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("mer0vech"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry = NULL;
    size_t entry_offset_byte = 0;
    size_t bytes_to_copy = 0;
    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    // if(wait_event_interruptible(dev->read_queue, 
    //     (dev->c_buffer.in_offs != dev->c_buffer.out_offs || dev->c_buffer.full))) {
    //         return -ERESTARTSYS;
    // }

    if(mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->c_buffer, *f_pos, &entry_offset_byte);
    if(entry == NULL) {
        mutex_unlock(&dev->lock);
        return 0;
    }

    bytes_to_copy = entry->size - entry_offset_byte;
    if(bytes_to_copy > count) {
        bytes_to_copy = count;
    }

    if(copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_copy)) {
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }

    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

    mutex_unlock(&dev->lock);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    char *text;
    size_t i;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);


    // check if input size not larger than allowed; else throw error
    if (count >= KMALLOC_MAX_SIZE) {
        return -EMSGSIZE;
    }

    // allocate memory for text; else throw error
    text = kmalloc(count + 1, GFP_KERNEL);
    if (!text) {
        return -ENOMEM;
    }

    // copy user buffer to text; else free memory and throw error
    if (copy_from_user(text, buf, count)) {
        kfree(text);
        return -EFAULT;
    }

    // add trailing null to string
    text[count] = '\0';

    if(mutex_lock_interruptible(&dev->lock)) {
        kfree(text);
        return -ERESTARTSYS;
    }

    for(i = 0; i < count; i++) {
        char c = text[i];

        // if(dev->tmp_size >= MAX_LINE_SUPPORTED) {
        //     printk(KERN_WARNING "aesd_dev: entry too big - refused! \n");
        //     mutex_unlock(&dev->lock);
        //     kfree(text);
        //     return -E2BIG;
        // }

        if(dev->tmp_size >= dev->tmp_capacity) {
            size_t new_capacity = dev->tmp_capacity + 32;
            char *new_tmp = krealloc(dev->tmp_buffer, new_capacity, GFP_KERNEL);
            if(!new_tmp) {
                mutex_unlock(&dev->lock);
                kfree(text);
                return -ENOMEM;
            }

            dev->tmp_buffer = new_tmp;
            dev->tmp_capacity = new_capacity;
        }

        dev->tmp_buffer[dev->tmp_size] = c;
        dev->tmp_size++;

        if(c == '\n') {
            struct aesd_buffer_entry new_entry;
            char *line_content = kmalloc(dev->tmp_size, GFP_KERNEL);
            if(!line_content) {
                mutex_unlock(&dev->lock);
                kfree(text);
                return -ENOMEM;
            }

            memcpy(line_content, dev->tmp_buffer, dev->tmp_size);
            new_entry.buffptr = line_content;
            new_entry.size = dev->tmp_size;

            char *to_free = NULL;
            if(dev->c_buffer.full) {
                to_free = (char *)dev->c_buffer.entry[dev->c_buffer.in_offs].buffptr;
            }

            aesd_circular_buffer_add_entry(&dev->c_buffer, &new_entry);
            printk(KERN_INFO "aesd_dev: entry added to buffer (size: %zu)\n", dev->tmp_size);

            if(to_free) {
                kfree(to_free);
            }

            kfree(dev->tmp_buffer);
            dev->tmp_buffer = NULL;
            dev->tmp_size = 0;
            dev->tmp_capacity = 0;

            // wake_up_interruptible(&dev->read_queue);
        }
    }

    mutex_unlock(&dev->lock);
    kfree(text);

    return count;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    aesd_circular_buffer_init(&aesd_device.c_buffer);
    mutex_init(&aesd_device.lock);
    init_waitqueue_head(&aesd_device.read_queue);
    aesd_device.tmp_capacity = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    struct aesd_buffer_entry *entry;
    int i;

    cdev_del(&aesd_device.cdev);

    if(aesd_device.tmp_buffer) {
        kfree(&aesd_device.tmp_buffer);
    }

    aesd_device.tmp_size = 0;
    aesd_device.tmp_capacity = 0;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.c_buffer, i) {
        if(entry->buffptr) {
            kfree(entry->buffptr);
            entry->buffptr = NULL;
        }
        entry->size = 0;
    }

    aesd_device.c_buffer.in_offs = 0;
    aesd_device.c_buffer.out_offs = 0;
    aesd_device.c_buffer.full = false;

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
