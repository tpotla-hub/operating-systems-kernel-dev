#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/skbuff.h>
#include <linux/freezer.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/version.h>
#include <linux/blkpg.h>
#include <linux/namei.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adil Ahmad");
MODULE_DESCRIPTION("Block-level Read/Write Abstraction for USB Storage");
MODULE_VERSION("1.0");

/* Path to USB device node — must be a global symbol for kmod-ioctl.c */
char *device = "/dev/sdb";
module_param(device, charp, S_IRUGO);
MODULE_PARM_DESC(device, "USB block device path");

/* USB block device handles (unused for now) */
static struct block_device *blk_dev  = NULL;
static struct bio          *bio_ptr  = NULL;
static struct file         *file_ptr = NULL;

bool  kmod_ioctl_init(void);
void  kmod_ioctl_teardown(void);

/* Establish communication with USB block device */
static bool usb_open(void)
{
    /* TODO: open file_ptr and blk_dev based on 'device' */
    return true;
}

/* Tear down communication with USB block device */
static void usb_close(void)
{
    /* TODO: release bio_ptr, blk_dev, and file_ptr */
}

static int __init kmod_init(void)
{
    pr_info("[kmod-main] Module loading\n");

    if (!usb_open()) {
        pr_err("[kmod-main] Unable to access %s\n", device);
        return -ENODEV;
    }

    if (!kmod_ioctl_init()) {
        pr_err("[kmod-main] IOCTL initialization failed\n");
        usb_close();
        return -EIO;
    }

    pr_info("[kmod-main] Initialization successful\n");
    return 0;
}

static void __exit kmod_exit(void)
{
    kmod_ioctl_teardown();
    usb_close();
    pr_info("[kmod-main] Module unloaded\n");
}

module_init(kmod_init);
module_exit(kmod_exit);


