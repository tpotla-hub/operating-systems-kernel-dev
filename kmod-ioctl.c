#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/cdev.h>
#include <linux/nospec.h>
#include <linux/vmalloc.h>

#include "../ioctl-defines.h"

/* Device numbers / class / cdev */
static dev_t dev = 0;
static struct class *kmod_class;
static struct cdev  kmod_cdev;

/* Global IOCTL request buffers */
struct block_rw_ops        rw_request;
struct block_rwoffset_ops  rwoffset_request;

/* From kmod-main.c */
extern char *device;
extern struct block_device *bdevice;
extern struct file *usb_file;

/* Protect IOCTLs */
static DEFINE_MUTEX(ioctl_mutex);
static unsigned long current_offset = 0;

bool  kmod_ioctl_init(void);
void  kmod_ioctl_teardown(void);

static long kmod_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    char *tmp_buf = NULL;
    struct file *usb_file_handle = NULL;
    struct block_device *block_dev = NULL;
    unsigned int len, off;
    int idx, result;

    mutex_lock(&ioctl_mutex);

    switch (cmd) {
    case BREAD:
    case BWRITE:
        if (copy_from_user(&rw_request, (void __user *)arg, sizeof(rw_request))) {
            printk("ERR: copy_from_user for RW failed\n");
            result = -EFAULT;
            goto out;
        }
        len = rw_request.size;
        if (!len) {
            printk("ERR: RW request size is zero\n");
            result = -EINVAL;
            goto out;
        }

        printk("IOCTL REQUEST: %s (buf=%p, len=%u, off=%lu)\n",
               cmd == BREAD ? "BREAD" : "BWRITE",
               rw_request.data, len, current_offset);

        tmp_buf = vmalloc(len);
        if (!tmp_buf) {
            printk("ERR: vmalloc(%u) failed\n", len);
            result = -ENOMEM;
            goto out;
        }

        if (cmd == BWRITE && copy_from_user(tmp_buf, rw_request.data, len)) {
            printk("ERR: copy_from_user for WRITE data failed\n");
            result = -EFAULT;
            goto cleanup_buf;
        }

        usb_file_handle = bdev_file_open_by_path(device,
                                                 BLK_OPEN_READ | BLK_OPEN_WRITE,
                                                 NULL, NULL);
        if (IS_ERR(usb_file_handle)) {
            printk("ERR: open device '%s' failed (%ld)\n",
                   device, PTR_ERR(usb_file_handle));
            result = -EIO;
            goto cleanup_buf;
        }

        block_dev = file_bdev(usb_file_handle);
        if (IS_ERR(block_dev)) {
            printk("ERR: cannot get block_device\n");
            result = -EIO;
            goto close_usb;
        }

        for (idx = 0; idx < len; idx += 512) {
            unsigned int chunk = min(512U, len - idx);
            struct bio *bio_req = bio_alloc(block_dev, 1,
                        (cmd == BREAD ? REQ_OP_READ : REQ_OP_WRITE),
                        GFP_NOIO);
            if (!bio_req) {
                printk("ERR: bio_alloc returned NULL\n");
                result = -ENOMEM;
                goto close_usb;
            }
            bio_set_dev(bio_req, block_dev);
            bio_req->bi_iter.bi_sector = (current_offset + idx) / 512;
            bio_req->bi_opf = (cmd == BREAD ? REQ_OP_READ : REQ_OP_WRITE);

            if (bio_add_page(bio_req,
                             vmalloc_to_page(tmp_buf + idx),
                             chunk,
                             offset_in_page(tmp_buf + idx)) != chunk) {
                printk("ERR: bio_add_page chunk %d failed\n", idx);
                bio_put(bio_req);
                result = -EIO;
                goto close_usb;
            }

            printk("Submitting BIO: %s sector=%lld len=%u\n",
                   cmd == BREAD ? "READ" : "WRITE",
                   bio_req->bi_iter.bi_sector, chunk);

            result = submit_bio_wait(bio_req);
            bio_put(bio_req);
            if (result < 0) {
                printk("ERR: submit_bio_wait returned %d\n", result);
                result = -EIO;
                goto close_usb;
            }
        }

        if (cmd == BREAD && copy_to_user(rw_request.data, tmp_buf, len)) {
            printk("ERR: copy_to_user for READ failed\n");
            result = -EFAULT;
            goto close_usb;
        }

        current_offset += len;
        result = len;
        break;

    case BREADOFFSET:
    case BWRITEOFFSET:
        if (copy_from_user(&rwoffset_request,
                           (void __user *)arg,
                           sizeof(rwoffset_request))) {
            printk("ERR: copy_from_user for RWOFFSET failed\n");
            result = -EFAULT;
            goto out;
        }
        len = rwoffset_request.size;
        off = rwoffset_request.offset;
        if (!len) {
            printk("ERR: RWOFFSET size is zero\n");
            result = -EINVAL;
            goto out;
        }

        printk("IOCTL REQUEST: %s (buf=%p, len=%u, off=%u)\n",
               cmd == BREADOFFSET ? "BREADOFFSET" : "BWRITEOFFSET",
               rwoffset_request.data, len, off);

        {
            unsigned int start_sec   = off / 512;
            unsigned int sec_off     = off % 512;
            unsigned int total       = len + sec_off;
            unsigned int nr_sectors  = DIV_ROUND_UP(total, 512);
            unsigned int alloc_len   = nr_sectors * 512;

            tmp_buf = vmalloc(alloc_len);
            if (!tmp_buf) {
                printk("ERR: vmalloc(%u) failed\n", alloc_len);
                result = -ENOMEM;
                goto out;
            }

            if (cmd == BWRITEOFFSET) {
                memset(tmp_buf, 0, alloc_len);
                if (copy_from_user(tmp_buf + sec_off,
                                   rwoffset_request.data,
                                   len)) {
                    printk("ERR: copy_from_user for WRITEOFFSET failed\n");
                    result = -EFAULT;
                    goto cleanup_buf;
                }
            }

            usb_file_handle = bdev_file_open_by_path(device,
                                                     BLK_OPEN_READ | BLK_OPEN_WRITE,
                                                     NULL, NULL);
            if (IS_ERR(usb_file_handle)) {
                printk("ERR: open device '%s' failed\n", device);
                result = -EIO;
                goto cleanup_buf;
            }

            block_dev = file_bdev(usb_file_handle);
            if (IS_ERR(block_dev)) {
                printk("ERR: cannot get block_device\n");
                result = -EIO;
                goto close_usb;
            }

            for (idx = 0; idx < alloc_len; idx += 512) {
                unsigned int chunk = min(512U, alloc_len - idx);
                struct bio *bio_req = bio_alloc(block_dev, 1,
                    (cmd == BREADOFFSET ? REQ_OP_READ : REQ_OP_WRITE),
                    GFP_NOIO);
                if (!bio_req) {
                    printk("ERR: bio_alloc returned NULL\n");
                    result = -ENOMEM;
                    goto close_usb;
                }
                bio_set_dev(bio_req, block_dev);
                bio_req->bi_iter.bi_sector = start_sec + idx / 512;
                bio_req->bi_opf = (cmd == BREADOFFSET ? REQ_OP_READ : REQ_OP_WRITE);

                if (bio_add_page(bio_req,
                                 vmalloc_to_page(tmp_buf + idx),
                                 chunk,
                                 offset_in_page(tmp_buf + idx)) != chunk) {
                    printk("ERR: bio_add_page offset chunk %d failed\n", idx);
                    bio_put(bio_req);
                    result = -EIO;
                    goto close_usb;
                }

                printk("Submitting BIO: %s sector=%lld len=%u\n",
                       cmd == BREADOFFSET ? "READ" : "WRITE",
                       bio_req->bi_iter.bi_sector, chunk);

                result = submit_bio_wait(bio_req);
                bio_put(bio_req);
                if (result < 0) {
                    printk("ERR: submit_bio_wait returned %d\n", result);
                    result = -EIO;
                    goto close_usb;
                }
            }

            if (cmd == BREADOFFSET &&
                copy_to_user(rwoffset_request.data,
                             tmp_buf + sec_off, len)) {
                printk("ERR: copy_to_user for READOFFSET failed\n");
                result = -EFAULT;
                goto close_usb;
            }

            current_offset = off + len;
            result = len;
        }
        break;

    default:
        printk("ERR: unsupported IOCTL cmd %u\n", cmd);
        result = -EINVAL;
        break;
    }

close_usb:
    if (usb_file_handle && !IS_ERR(usb_file_handle))
        bdev_fput(usb_file_handle);
cleanup_buf:
    vfree(tmp_buf);
out:
    mutex_unlock(&ioctl_mutex);
    return result;
}

static int kmod_open(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "kmod: device opened\n");
    return 0;
}

static int kmod_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "kmod: device closed\n");
    return 0;
}

static const struct file_operations fops = {
    .owner           = THIS_MODULE,
    .open            = kmod_open,
    .release         = kmod_release,
    .unlocked_ioctl  = kmod_ioctl,
};

bool kmod_ioctl_init(void)
{
    if (alloc_chrdev_region(&dev, 0, 1, "usbaccess") < 0) {
        printk(KERN_ERR "kmod: alloc_chrdev_region failed\n");
        return false;
    }
    cdev_init(&kmod_cdev, &fops);
    if (cdev_add(&kmod_cdev, dev, 1) < 0) {
        printk(KERN_ERR "kmod: cdev_add failed\n");
        unregister_chrdev_region(dev, 1);
        return false;
    }
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,2,16)
    kmod_class = class_create(THIS_MODULE, "kmod_class");
#else
    kmod_class = class_create("kmod_class");
#endif
    if (IS_ERR(kmod_class)) {
        printk(KERN_ERR "kmod: class_create failed\n");
        cdev_del(&kmod_cdev);
        unregister_chrdev_region(dev, 1);
        return false;
    }
    if (!device_create(kmod_class, NULL, dev, NULL, "kmod")) {
        printk(KERN_ERR "kmod: device_create failed\n");
        class_destroy(kmod_class);
        cdev_del(&kmod_cdev);
        unregister_chrdev_region(dev, 1);
        return false;
    }
    printk(KERN_INFO "[kmod] IOCTL initialization complete\n");
    return true;
}

void kmod_ioctl_teardown(void)
{
    device_destroy(kmod_class, dev);
    class_destroy(kmod_class);
    cdev_del(&kmod_cdev);
    unregister_chrdev_region(dev, 1);
    printk(KERN_INFO "[kmod] IOCTL teardown complete\n");
}


