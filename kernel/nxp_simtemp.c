/*
 * nxp_simtemp.c
 *
 * Simtemp minimal driver (Step 2: add hrtimer for periodic sampling).
 *
 * This driver registers a simple platform device and exposes a misc character
 * device at /dev/simtemp. Samples are produced periodically with an hrtimer
 * and returned to user space via blocking reads.
 *
 * SPDX-License-Identifier: GPL
 *
 * Author: Miguel Elibert Ibarra Rodriguez <ibarramiguel119@gmail.com>
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
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>

#define SIMTEMP_FLAG_NEW_SAMPLE           (1u << 0)
#define SIMTEMP_FLAG_THRESHOLD_CROSSED    (1u << 1)
#define SIMTEMP_BUF_SIZE 64

/* public sample layout (packed, stable binary record) */
struct simtemp_sample {
	__u64 timestamp_ns;   /* monotonic timestamp */
	__s32 temp_mC;        /* milli-degree Celsius */
	__u32 flags;          /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed));

struct simdev {
	struct miscdevice mdev;
	int threshold_mC;

	/* buffer of samples */
	struct simtemp_sample buf[SIMTEMP_BUF_SIZE];
	int head, tail;
	struct mutex lock;
	wait_queue_head_t wq;

	/* hrtimer */
	struct hrtimer timer;
	ktime_t interval;
};

static struct simdev *gdev;
static struct platform_device *g_test_pdev;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                */
/* ---------------------------------------------------------------------- */

/* Generate a temperature sample (random jitter around 44.000°C) */
static int produce_temperature_mC(void)
{
	int base = 44000; /* 44.000 C */
	int jitter = 0;
	get_random_bytes(&jitter, sizeof(jitter));
	jitter = jitter % 500; /* 0..499 */
	return base + (jitter - 250); /* -250..+249 */
}

/* Timer callback: push new sample into ring buffer */
static enum hrtimer_restart sim_timer_cb(struct hrtimer *t)
{
    struct simdev *s = container_of(t, struct simdev, timer);
    struct simtemp_sample sample;
    static int last_temp = 0;

    sample.timestamp_ns = (u64)ktime_get_ns();
    sample.temp_mC = produce_temperature_mC();
    sample.flags = SIMTEMP_FLAG_NEW_SAMPLE;

    /* Detect threshold crossing */
    if ((last_temp < s->threshold_mC && sample.temp_mC >= s->threshold_mC) ||
        (last_temp >= s->threshold_mC && sample.temp_mC < s->threshold_mC)) {
        sample.flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
    }
    last_temp = sample.temp_mC;

    mutex_lock(&s->lock);
    s->buf[s->head] = sample;
    s->head = (s->head + 1) % SIMTEMP_BUF_SIZE;
    if (s->head == s->tail) // overwrite oldest
        s->tail = (s->tail + 1) % SIMTEMP_BUF_SIZE;
    mutex_unlock(&s->lock);

    wake_up_interruptible(&s->wq); // Despertar lectores/pollers

    hrtimer_forward_now(&s->timer, s->interval);
    return HRTIMER_RESTART;
}

/* ---------------------------------------------------------------------- */
/* File operations                                                        */
/* ---------------------------------------------------------------------- */

static ssize_t sim_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct simdev *s = gdev;
	struct simtemp_sample sample;
	ssize_t want = sizeof(sample);

	if (count < want)
		return -EINVAL;

	/* wait until data available */
	if (s->head == s->tail) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(s->wq, s->head != s->tail))
			return -ERESTARTSYS;
	}

	mutex_lock(&s->lock);
	sample = s->buf[s->tail];
	s->tail = (s->tail + 1) % SIMTEMP_BUF_SIZE;
	mutex_unlock(&s->lock);

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

static unsigned int sim_poll(struct file *file, poll_table *wait)
{
    struct simdev *s = gdev;
    unsigned int mask = 0;

    poll_wait(file, &s->wq, wait);

    mutex_lock(&s->lock);
    if (s->head != s->tail)
        mask |= POLLIN | POLLRDNORM;
    mutex_unlock(&s->lock);

    return mask;
}

static const struct file_operations sim_fops = {
	.owner          = THIS_MODULE,
	.read           = sim_read,
	.open           = sim_open,
	.release        = sim_release,
	.unlocked_ioctl = sim_ioctl,
	.poll		    = sim_poll,
};

/* ---------------------------------------------------------------------- */
/* Platform driver                                                        */
/* ---------------------------------------------------------------------- */

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
	gdev = s;

	mutex_init(&s->lock);
	init_waitqueue_head(&s->wq);

	s->interval = ktime_set(0, 100 * 1000000); /* 100 ms default */
	hrtimer_init(&s->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	s->timer.function = sim_timer_cb;
	hrtimer_start(&s->timer, s->interval, HRTIMER_MODE_REL);

	pr_info("simtemp: /dev/%s ready\n", s->mdev.name);
	return 0;
}

static void sim_remove(struct platform_device *pdev)
{
	struct simdev *s = platform_get_drvdata(pdev);

	if (!s)
		return;

	hrtimer_cancel(&s->timer);
	misc_deregister(&s->mdev);
	kfree(s);
	gdev = NULL;
}

static struct platform_driver sim_driver = {
	.probe  = sim_probe,
	.remove = sim_remove,
	.driver = {
		.name  = "nxp_simtemp",
		.owner = THIS_MODULE,
	},
};

/* ---------------------------------------------------------------------- */
/* Module init/exit                                                       */
/* ---------------------------------------------------------------------- */

static int __init sim_init(void)
{
	int ret;

	ret = platform_driver_register(&sim_driver);
	if (ret) {
		pr_err("simtemp: platform_driver_register failed %d\n", ret);
		return ret;
	}

	/* test device so probe() runs without DT */
	g_test_pdev = platform_device_register_simple("nxp_simtemp", -1, NULL, 0);
	if (IS_ERR(g_test_pdev)) {
		pr_warn("simtemp: could not create test platform_device (continuing)\n");
		g_test_pdev = NULL;
	}

	pr_info("simtemp: module loaded\n");
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
MODULE_AUTHOR("Miguel Elibert Ibarra Rodriguez <ibarramiguel119@gmail.com>");
MODULE_DESCRIPTION("nxp_simtemp - step2: /dev/simtemp with periodic hrtimer sampling");
MODULE_VERSION("0.2");
