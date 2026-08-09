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
#include "aesd_ioctl.h"

// #define MAX_LINE_SUPPORTED 4096

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("mer0vech"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

static size_t get_seek_to_offset(struct aesd_circular_buffer *buffer, int target_cmd_index, int target_cmd_offset, bool *match)
{
    size_t accumulated_size = 0;
    int current_index = buffer->out_offs;
    int cmd_counter = 0;
    int total_entries = 0;

    if(match) {
        *match = false;
    }

    if (buffer->full) {
        total_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else {
        total_entries = (buffer->in_offs >= buffer->out_offs) ? 
                        (buffer->in_offs - buffer->out_offs) : 
                        (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs + buffer->in_offs);
    }

    if (target_cmd_index < 0 || target_cmd_index >= total_entries) {
        return 0;
    }

    while (cmd_counter < target_cmd_index) {
        struct aesd_buffer_entry *seek = &(buffer->entry[current_index]);
        
        if (seek->buffptr == NULL) {
            return 0; 
        }

        accumulated_size += seek->size;
        current_index = (current_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        cmd_counter++;
    }

    struct aesd_buffer_entry *target_entry = &(buffer->entry[current_index]);

    if (target_cmd_offset < 0 || target_cmd_offset >= target_entry->size) {
        return 0; 
    }

    if (match) {
        *match = true;
    }

    accumulated_size += target_cmd_offset;

    return accumulated_size;
}

static size_t get_total_buffer_size(struct aesd_circular_buffer *buffer)
{
    size_t total_size = 0;
    int current_index = buffer->out_offs;
    int counter = 0;

    while (counter < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        if (!buffer->full && (buffer->in_offs == buffer->out_offs)) {
            break;
        }

        struct aesd_buffer_entry *entry = &(buffer->entry[current_index]);
        if (entry->buffptr == NULL) {
            break;
        }

        total_size += entry->size;
        current_index = (current_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        counter++;
        if (!buffer->full && current_index == buffer->in_offs) {
            break;
        }
    }
    return total_size;
}

static loff_t aesdchar_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t new_position;
    loff_t current_buffer_size = 0;

    if(mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    switch(whence) {
        case SEEK_SET:
            new_position = offset;
            break;
        case SEEK_CUR:
            new_position = filp->f_pos + offset;
            break;
        case SEEK_END:
            current_buffer_size = get_total_buffer_size(&dev->c_buffer);
            new_position = dev->bytes_dropped + (loff_t)current_buffer_size + offset;
            break;
        default:
            mutex_unlock(&dev->lock);
            return -EINVAL;
    }

    if(new_position < 0 || new_position < dev->bytes_dropped) {
        mutex_unlock(&dev->lock);
        return (new_position < 0) ? -EINVAL : -EPIPE;;
    }

    filp->f_pos = new_position;

    mutex_unlock(&dev->lock);

    return new_position;
}

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
    loff_t rel_fpos;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if(mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    if(*f_pos < dev->bytes_dropped) {
        mutex_unlock(&dev->lock);
        return -EPIPE;
    }

    rel_fpos = *f_pos - dev->bytes_dropped;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->c_buffer, rel_fpos, &entry_offset_byte);
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
                struct aesd_buffer_entry *old_entry = &dev->c_buffer.entry[dev->c_buffer.in_offs];
                to_free = (char *)old_entry->buffptr;
                dev->bytes_dropped += old_entry->size;
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

        }
    }

    mutex_unlock(&dev->lock);
    kfree(text);

    return count;
}

static long aesd_ioctl(struct file *filp, uint32_t cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto k_seek;
    loff_t current_buffer_size = 0;
    bool match;

    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            if(copy_from_user(&k_seek, (struct aesd_seekto __user *)arg, sizeof(struct aesd_seekto))) {
                return -EFAULT;
            }

            if(mutex_lock_interruptible(&dev->lock)) {
                return -ERESTARTSYS;
            }

            current_buffer_size = get_seek_to_offset(&dev->c_buffer, k_seek.write_cmd, k_seek.write_cmd_offset, &match);

            if(!match) {
                mutex_unlock(&dev->lock);
                return -EINVAL;
            }

            filp->f_pos = dev->bytes_dropped + (loff_t)current_buffer_size;
            printk(KERN_INFO "aesdchar: IOCTL seekto completed - cmd: %u, new f_pos: %lld\n", 
                k_seek.write_cmd, filp->f_pos);

            mutex_unlock(&dev->lock);

            return 0;

        default:
            return -ENOTTY;
    }

}

struct file_operations aesd_fops = {
    .owner          = THIS_MODULE,
    .llseek         = aesdchar_llseek,
    .read           = aesd_read,
    .write          = aesd_write,
    .unlocked_ioctl = aesd_ioctl,
    .open           = aesd_open,
    .release        = aesd_release,
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
    aesd_device.bytes_dropped = 0;

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
