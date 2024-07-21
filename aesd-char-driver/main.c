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
#include <linux/slab.h>		      // 

#include "aesdchar.h"
#include "aesd_ioctl.h"
#include "access_ok_version.h"

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

    if(*f_pos >= aesd_circular_buffer_size(dev->cb_rec_data))
    {
	dev->read_byte_count = 0;
        retval = 0;
    }
    else if((cur_entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->cb_rec_data, *f_pos, &read_offs)) == NULL)
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

        *f_pos += bytes_to_read;
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

    if(dev->entry->size > 0 && strnstr(dev->entry->buffptr, "\n", dev->entry->size) != NULL)
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

loff_t aesd_lseek(struct file *filp, loff_t off, int whence)
{
	struct aesd_dev* dev = filp->private_data;
	loff_t buff_size = 0;

	if(mutex_lock_interruptible(&dev->lock) != 0)
        	return -EINVAL;

        buff_size = (loff_t)aesd_circular_buffer_size(dev->cb_rec_data);
        mutex_unlock(&dev->lock);

	return fixed_size_llseek(filp, off, whence, buff_size);
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0, err = 0;
	struct aesd_seekto seekto;
	struct aesd_dev* dev = filp->private_data;
	long cb_offset = 0;

	if(_IOC_TYPE(cmd) != AESD_IOC_MAGIC || _IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));

	if (err) 
		return -EFAULT;

	switch(cmd)
	{
	case AESDCHAR_IOCSEEKTO:
        	if(copy_from_user(&seekto, (const void __user *) arg, sizeof(seekto)) != 0)
        	{
			ret = -EFAULT;
		}
		else
		{
			if(mutex_lock_interruptible(&dev->lock) != 0)
                		return -EINVAL;

			cb_offset = (long) aesd_circular_buffer_find_offset(dev->cb_rec_data, (size_t)seekto.write_cmd, (size_t)seekto.write_cmd_offset);
			mutex_unlock(&dev->lock);

			if(cb_offset < 0)
			{
				return -ENOTTY;
			}
			else
			{
				ret = filp->f_pos = cb_offset;
			}
		}

		break;

	default:
		return -ENOTTY;
	}

	return ret;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .llseek =   aesd_lseek,
    .unlocked_ioctl = aesd_ioctl,
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
