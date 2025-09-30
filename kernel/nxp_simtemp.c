/*
 * nxp_simtemp_step1_minimal.c
 *
 * Step 1 (minimal): platform driver + test platform device + misc char device
 * - probe() registers /dev/simtemp (miscdevice)
 * - read() returns one binary struct simtemp_sample per read
 * - no timer / ring buffer / poll yet; each read generates a fresh sample
 *
 * Build (simple): create a Makefile that builds this file as an out-of-tree module
 *  obj-m += nxp_simtemp_step1_minimal.o
 *  KDIR ?= /lib/modules/$(shell uname -r)/build
 *  PWD := $(shell pwd)
 *  all:
 *  	$(MAKE) -C $(KDIR) M=$(PWD) modules
 *  clean:
 *  	$(MAKE) -C $(KDIR) M=$(PWD) clean
 *
 * Test:
 *  $ make
 *  $ sudo insmod nxp_simtemp_step1_minimal.ko
 *  $ ls -l /dev/simtemp
 *  $ hexdump -C -n 16 /dev/simtemp    # read one binary sample
 *  $ sudo rmmod nxp_simtemp_step1_minimal
 *
 * Notes:
 *  - This file is intentionally small and single-instance. It is meant as
 *    the starting point for iterative feature additions (timer, ring buffer,
 *    poll/wakeup, sysfs, DT properties, ioctl, stats, etc.).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/random.h>
#include <linux/types.h>

/* public sample layout (packed, stable binary record) */
struct simtemp_sample {
	__u64 timestamp_ns;   /* monotonic timestamp */
	__s32 temp_mC;        /* milli-degree Celsius */
	__u32 flags;          /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed));

#define SIMTEMP_FLAG_NEW_SAMPLE           (1u << 0)
#define SIMTEMP_FLAG_THRESHOLD_CROSSED    (1u << 1)

struct simdev {
	struct miscdevice mdev;
	/* simple fields for this minimal step */
	int threshold_mC;
};

static struct simdev *gdev;
static struct platform_device *g_test_pdev;

/* produce a temperature sample (simple random jitter around 44.000°C) */
static int produce_temperature_mC(void)
{
	int base = 44000; /* 44.000 C */
	int jitter = 0;
	get_random_bytes(&jitter, sizeof(jitter));
	jitter = jitter % 500; /* 0..499 */
	return base + (jitter - 250); /* -250..+249 */
}

/* file operations */
static ssize_t sim_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct simtemp_sample sample;
	ssize_t want = sizeof(sample);

	if (count < want)
		return -EINVAL;

	/* build sample on-the-fly */
	sample.timestamp_ns = (u64)ktime_get_ns();
	sample.temp_mC = produce_temperature_mC();
	sample.flags = SIMTEMP_FLAG_NEW_SAMPLE;

	if (copy_to_user(buf, &sample, want))
		return -EFAULT;

	return want;
}

static int sim_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int sim_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long sim_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}

static const struct file_operations sim_fops = {
	.owner = THIS_MODULE,
	.read = sim_read,
	.open = sim_open,
	.release = sim_release,
	.unlocked_ioctl = sim_ioctl,
};

/* platform probe/remove: register miscdev on probe */
static int sim_probe(struct platform_device *pdev)
{
	struct simdev *s;
	int ret;

	pr_info("simtemp: probe called\n");

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->threshold_mC = 45000; /* default */

	s->mdev.minor = MISC_DYNAMIC_MINOR;
	s->mdev.name = "simtemp";
	s->mdev.fops = &sim_fops;

	ret = misc_register(&s->mdev);
	if (ret) {
		pr_err("simtemp: misc_register failed %d\n", ret);
		kfree(s);
		return ret;
	}

	platform_set_drvdata(pdev, s);
	/* keep a global pointer for this simple single-instance example */
	gdev = s;

	pr_info("simtemp: /dev/%s ready\n", s->mdev.name);
	return 0;
}

static void sim_remove(struct platform_device *pdev)
{
    struct simdev *s = platform_get_drvdata(pdev);

    if (!s)
        return;

    misc_deregister(&s->mdev);
    kfree(s);
    gdev = NULL;
}

static struct platform_driver sim_driver = {
	.probe = sim_probe,
	.remove = sim_remove,
	.driver = {
		.name = "nxp_simtemp",
		.owner = THIS_MODULE,
	},
};

static int __init sim_init(void)
{
	int ret;

	ret = platform_driver_register(&sim_driver);
	if (ret) {
		pr_err("simtemp: platform_driver_register failed %d\n", ret);
		return ret;
	}

	/* register a small test platform device so probe() runs even without DT */
	g_test_pdev = platform_device_register_simple("nxp_simtemp", -1, NULL, 0);
	if (IS_ERR(g_test_pdev)) {
		pr_warn("simtemp: could not create test platform_device (continuing)\n");
		g_test_pdev = NULL;
	}

	pr_info("simtemp: module loaded (minimal step 1)\n");
	return 0;
}

static void __exit sim_exit(void)
{
	if (g_test_pdev)
		platform_device_unregister(g_test_pdev);

	platform_driver_unregister(&sim_driver);
	pr_info("simtemp: module unloaded\n");
}

module_init(sim_init);
module_exit(sim_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MIguel Elibert Ibarra Rodriguez <ibarramiguel119@gmail.com>");
MODULE_DESCRIPTION("nxp_simtemp - minimal step1: /dev/simtemp with one-shot read");
MODULE_VERSION("0.1");
