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
#include <linux/of.h>
#include <linux/of_device.h>


#define SIMTEMP_FLAG_NEW_SAMPLE           (1u << 0)
#define SIMTEMP_FLAG_THRESHOLD_CROSSED    (1u << 1)
#define SIMTEMP_BUF_SIZE 64


/*operating modes */
enum sim_mode{
	SIM_MODE_NORMAL = 0,
	SIM_MODE_NOISY,
	SIM_MODE_RAMP,

};

/* public sample layout (packed, stable binary record) */
struct simtemp_sample {
	__u64 timestamp_ns;   /* monotonic timestamp */
	__s32 temp_mC;        /* milli-degree Celsius */
	__u32 flags;          /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed));

struct simdev {
	struct miscdevice mdev;
	int threshold_mC;

	/* mode + stats */
    enum sim_mode mode;
    u64 updates;
    u64 alerts;
    u64 last_error;

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
    struct timespec64 ts;

    /*get real time*/
    ktime_get_real_ts64(&ts);
    sample.timestamp_ns = (u64)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;

    
    switch (s->mode) {
    case SIM_MODE_NORMAL:
        sample.temp_mC = produce_temperature_mC();
        break;
    case SIM_MODE_NOISY:
        sample.temp_mC = produce_temperature_mC() + (get_random_u32() % 2000 - 1000);
        break;
    case SIM_MODE_RAMP:
    default:
        static int ramp = 30000;
        ramp += 100;
        if (ramp > 80000)
            ramp = 30000;
        sample.temp_mC = ramp;
        break;
    }

    sample.flags = SIMTEMP_FLAG_NEW_SAMPLE;
    s->updates++;

    if ((last_temp < s->threshold_mC && sample.temp_mC >= s->threshold_mC) ||
        (last_temp >= s->threshold_mC && sample.temp_mC < s->threshold_mC)) {
        sample.flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
        s->alerts++;
    }
    last_temp = sample.temp_mC;

    mutex_lock(&s->lock);
    s->buf[s->head] = sample;
    s->head = (s->head + 1) % SIMTEMP_BUF_SIZE;
    if (s->head == s->tail)
        s->tail = (s->tail + 1) % SIMTEMP_BUF_SIZE;
    mutex_unlock(&s->lock);

    wake_up_interruptible(&s->wq);

    hrtimer_forward_now(&s->timer, s->interval);
    return HRTIMER_RESTART;
}

/* ---------------------------------------------------------------------- */
/* Sysfs attributes                                                       */
/* ---------------------------------------------------------------------- */
/* sampling_ms */
static ssize_t sampling_ms_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct simdev *s = gdev;
    return sprintf(buf, "%lld\n", ktime_to_ms(s->interval));
}

static ssize_t sampling_ms_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    struct simdev *s = gdev;
    unsigned long val;
    if (kstrtoul(buf, 10, &val))
        return -EINVAL;

    if (val == 0)
        return -EINVAL;

    s->interval = ktime_set(0, val * 1000000ULL);
    hrtimer_cancel(&s->timer);
    hrtimer_start(&s->timer, s->interval, HRTIMER_MODE_REL);
    return count;
}
static DEVICE_ATTR_RW(sampling_ms);

/* threshold_mC */
static ssize_t threshold_mC_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", gdev->threshold_mC);
}
static ssize_t threshold_mC_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 10, &val))
        return -EINVAL;
    gdev->threshold_mC = val;
    return count;
}
static DEVICE_ATTR_RW(threshold_mC);

/* mode */
static ssize_t mode_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct simdev *s = gdev;
    switch (s->mode) {
    case SIM_MODE_NORMAL: return sprintf(buf, "normal\n");
    case SIM_MODE_NOISY:  return sprintf(buf, "noisy\n");
    case SIM_MODE_RAMP:   return sprintf(buf, "ramp\n");
    }
    return sprintf(buf, "unknown\n");
}
static ssize_t mode_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    struct simdev *s = gdev;
    if (sysfs_streq(buf, "normal"))
        s->mode = SIM_MODE_NORMAL;
    else if (sysfs_streq(buf, "noisy"))
        s->mode = SIM_MODE_NOISY;
    else if (sysfs_streq(buf, "ramp"))
        s->mode = SIM_MODE_RAMP;
    else
        return -EINVAL;
    return count;
}
static DEVICE_ATTR_RW(mode);

/* stats (RO) */
static ssize_t stats_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct simdev *s = gdev;
    return sprintf(buf,
        "updates=%llu alerts=%llu last_error=%llu\n",
        s->updates, s->alerts, s->last_error);
}
static DEVICE_ATTR_RO(stats);


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
    struct device_node *np = pdev->dev.of_node;
    u32 val;
    int ret;

    pr_info("simtemp: probe called\n");

    s = kzalloc(sizeof(*s), GFP_KERNEL);
    if (!s)
        return -ENOMEM;

    /* defaults */
    s->threshold_mC = 45000;                /* 45.0 °C */
    s->interval = ktime_set(0, 100 * 1000000ULL);  /* 100 ms */

    /* override from DT if present */
    if (np) {
        if (!of_property_read_u32(np, "sampling-ms", &val) && val > 0)
            s->interval = ktime_set(0, val * 1000000ULL);

        if (!of_property_read_u32(np, "threshold-mC", &val))
            s->threshold_mC = val;
    }

    /* misc device setup */
    s->mdev.minor = MISC_DYNAMIC_MINOR;
    s->mdev.name = "simtemp";
    s->mdev.fops = &sim_fops;

    ret = misc_register(&s->mdev);
    if (ret) {
        pr_err("simtemp: misc_register failed %d\n", ret);
        kfree(s);
        return ret;
    }

    ret = device_create_file(s->mdev.this_device, &dev_attr_sampling_ms);
    ret |= device_create_file(s->mdev.this_device, &dev_attr_threshold_mC);
    ret |= device_create_file(s->mdev.this_device, &dev_attr_mode);
    ret |= device_create_file(s->mdev.this_device, &dev_attr_stats);
    if (ret)
        pr_warn("simtemp: failed to create sysfs attributes\n");

    platform_set_drvdata(pdev, s);
    gdev = s;

    mutex_init(&s->lock);
    init_waitqueue_head(&s->wq);

    /* start timer with current interval */
    hrtimer_init(&s->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    s->timer.function = sim_timer_cb;
    hrtimer_start(&s->timer, s->interval, HRTIMER_MODE_REL);

    pr_info("simtemp: /dev/%s ready (interval=%lld ms, threshold=%d mC)\n",
            s->mdev.name, ktime_to_ms(s->interval), s->threshold_mC);
    return 0;
}

static void sim_remove(struct platform_device *pdev)
{
	struct simdev *s = platform_get_drvdata(pdev);

	if (!s)
		return;
	device_remove_file(s->mdev.this_device, &dev_attr_sampling_ms);
	device_remove_file(s->mdev.this_device, &dev_attr_threshold_mC);
	device_remove_file(s->mdev.this_device, &dev_attr_mode);
	device_remove_file(s->mdev.this_device, &dev_attr_stats);

	hrtimer_cancel(&s->timer);
	misc_deregister(&s->mdev);
	kfree(s);
	gdev = NULL;
}

static const struct of_device_id sim_of_match[] = {
    { .compatible = "nxp,simtemp", },
    { /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, sim_of_match);

static struct platform_driver sim_driver = {
	.probe  = sim_probe,
	.remove = sim_remove,
	.driver = {
		.name  = "nxp_simtemp",
		.owner = THIS_MODULE,
		.of_match_table = sim_of_match, 
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

#ifndef CONFIG_OF
    /* Si no hay soporte OF/DT, registramos un fake device */
    g_test_pdev = platform_device_register_simple("nxp_simtemp", -1, NULL, 0);
    if (IS_ERR(g_test_pdev)) {
        pr_err("simtemp: failed to create test platform_device\n");
        g_test_pdev = NULL;
    } else {
        pr_info("simtemp: test platform_device registered (no DT)\n");
    }
#endif

    pr_info("simtemp: module loaded\n");
    return 0;
}

static void __exit sim_exit(void)
{
	#ifndef CONFIG_OF
		if (g_test_pdev) {
			platform_device_unregister(g_test_pdev);
			g_test_pdev = NULL;
		}
	#endif
    platform_driver_unregister(&sim_driver);
    pr_info("simtemp: module unloaded\n");
}

module_init(sim_init);
module_exit(sim_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miguel Elibert Ibarra Rodriguez <ibarramiguel119@gmail.com>");
MODULE_DESCRIPTION("nxp_simtemp: /dev/simtemp with periodic hrtimer sampling");
MODULE_VERSION("0.2");
