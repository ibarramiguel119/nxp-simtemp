#ifndef _NXP_SIMTEMP_H
#define _NXP_SIMTEMP_H

#include <linux/types.h>

#define SIMTEMP_DEV_NAME "simtemp"

struct simtemp_sample {
	__u64 timestamp_ns;   /* monotonic timestamp */
	__s32 temp_mC;        /* milli-degree Celsius */
	__u32 flags;          /* bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED */
} __attribute__((packed));

#define SIMTEMP_FLAG_NEW_SAMPLE        (1U << 0)
#define SIMTEMP_FLAG_THRESHOLD_CROSSED (1U << 1)

#endif /* _NXP_SIMTEMP_H */