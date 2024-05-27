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
#include <linux/string.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Matheus L. Ogata - matheus.ogata@outlook.com"); /** fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * handle open
     */
    struct aesd_dev *dev = NULL;
	
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * handle read
     */

    size_t read_offs = 0;
    size_t bytes_to_read = 0;
    struct aesd_dev* dev = filp->private_data;
    struct aesd_buffer_entry* cur_entry = NULL;

    if(mutex_lock_interruptible(&dev->lock) != 0)
        return -ERESTARTSYS;
	
    if((cur_entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->cb_rec_data, dev->read_byte_count, &read_offs)) == NULL)
    {	
	dev->read_byte_count = 0;
	retval = 0;
    }
    else
    {

	if((bytes_to_read = (cur_entry->size - read_offs)) > count)
		bytes_to_read = count;

	if(copy_to_user(buf, (char *)(cur_entry->buffptr + read_offs), bytes_to_read) != 0)
	{
		retval = -EFAULT;
		goto error_read;
	}
	
	dev->read_byte_count += bytes_to_read;

        f_pos = (loff_t *)aesd_circular_buffer_find_entry_offset_for_fpos(dev->cb_rec_data, dev->read_byte_count, &read_offs);
	retval = bytes_to_read;
    }
	
    mutex_unlock(&dev->lock);
    return retval;

error_read:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     *  handle write
     */

    struct aesd_dev* dev = filp->private_data;
    char* aux_buff = NULL;
    size_t buf_size = 0;
	
    if(mutex_lock_interruptible(&dev->lock) != 0)
    	return -ERESTARTSYS;

    if(dev->entry == NULL)
    {
	dev->entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
	if(dev->entry == NULL)
		goto error_write;

	dev->entry->buffptr = kmalloc(count, GFP_KERNEL);
	if(dev->entry->buffptr == NULL)
	{
		kfree(dev->entry);
		dev->entry = NULL;
		goto error_write;
	}

	if(copy_from_user((char *)dev->entry->buffptr, buf, count) != 0)
	{
		kfree(dev->entry);
		kfree(dev->entry->buffptr);
		dev->entry = NULL;
		retval = -EFAULT;
		goto error_write;
	}
	dev->entry->size = count;
    }
    else
    {
	buf_size = dev->entry->size + count;
	aux_buff = (char *)dev->entry->buffptr;
	dev->entry->buffptr = kmalloc(buf_size, GFP_KERNEL);
	if(dev->entry->buffptr == NULL)
        	goto error_write;

	if(copy_from_user((char *)(dev->entry->buffptr + dev->entry->size), buf, count) != 0)
	{
		kfree(dev->entry->buffptr);
		dev->entry->buffptr = aux_buff;
		retval = -EFAULT;
		goto error_write;
	}
	else
	{
	        memcpy((char *)dev->entry->buffptr, aux_buff, dev->entry->size);
		dev->entry->size = buf_size;
		kfree(aux_buff);
	}
    }

    retval = count;

    if(dev->entry->size > 0 && strstr(dev->entry->buffptr, "\n") != NULL)
    {
    	if((aux_buff = aesd_circular_buffer_add_entry(dev->cb_rec_data, dev->entry)) != NULL)
	{
		kfree(aux_buff);
	}
	kfree(dev->entry);
	dev->entry = NULL;
    }
    
    mutex_unlock(&dev->lock);
    return retval;

error_write:
    mutex_unlock(&dev->lock);
    return retval;

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

    /**
     * initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.lock);
    aesd_device.cb_rec_data = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    aesd_circular_buffer_init(aesd_device.cb_rec_data);
    aesd_device.entry = NULL;
    aesd_device.read_byte_count = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * cleanup AESD specific poritions here as necessary
     */
    struct  circ_buffer_foreach idx;
    struct aesd_buffer_entry* entry_ptr = NULL;
    
    AESD_CIRCULAR_BUFFER_FOREACH(entry_ptr, aesd_device.cb_rec_data, idx)
    {
	    if(entry_ptr->buffptr != NULL)
		    kfree(entry_ptr->buffptr);	
    }

    kfree(aesd_device.cb_rec_data);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
