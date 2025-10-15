p_simtemp

**Simulated Temperature Sensor Linux Kernel Driver**

---

## Overview

`nxp_simtemp` is a **minimal Linux kernel driver** that simulates a temperature sensor device.  
It provides:

- A **misc device**: `/dev/simtemp`  
- **Sysfs attributes** for runtime configuration  
- A **Python CLI** (`simtemp_cli.py`) for user-space interaction and testing  

The driver periodically generates temperature samples using an **hrtimer**, allowing user-space to read them as if a real sensor were connected.

---

##Features

- Configurable **sampling interval**, **threshold**, and **operating mode** (`normal`, `noisy`, `ramp`)  
- User-space access via `/dev/simtemp` (supports `read()` and `poll()`)  
- Sysfs attributes for configuration  
- Device Tree (DT) bindings for initial setup  
- Optional CLI utility for testing and automation  

---

## Quick Start

### 1. Build and Load the Module

```bash
make
sudo insmod nxp_simtemp.ko
```

Check kernel log:
```bash
dmesg | grep simtemp
```

---

### 2. Verify Device Node

```bash
ls -l /dev/simtemp
```

---

### 3. Read Temperature Samples

```bash
cat /dev/simtemp
```

Or use the Python CLI:

```bash
python3 simtemp_cli.py --count 5
```

---

### 4. Change Configuration

```bash
# Change sampling period (ms)
echo 200 > /sys/class/misc/simtemp/sampling_ms

# Change operating mode
echo noisy > /sys/class/misc/simtemp/mode
```

---

## CLI Tool

The `simtemp_cli.py` script provides a convenient interface for testing:

```bash
python3 simtemp_cli.py --sampling-ms 200 --threshold-mC 46000 --mode noisy
python3 simtemp_cli.py --show-sysfs
python3 simtemp_cli.py --test
```

---

## Repository Structure

```
nxp_simtemp/
├── docs
│   ├── AI_NOTES.md
│   ├── DESIGN.md
│   ├── README.md
│   └── TESTPLAN.md
├── kernel
│   ├── build
│   │   ├── Makefile -> ../Makefile
│   │   └── nxp_simtemp_step1_minimal.c -> ../nxp_simtemp_step1_minimal.c
│   ├── dts
│   │   └── nxp-simtemp.dtsi
│   ├── Makefile
│   ├── nxp_simtemp.c
│   ├── nxp_simtemp.h
│   └── nxp_simtemp_ioctl.h
├── meta-simtemp
│   ├── conf
│   │   └── layer.conf
│   └── recipes-kernel
│       ├── linux
│       │   ├── files
│       │   │   └── simtemp-overlay.dts
│       │   └── linux-imx_%.bbappend
│       ├── simtemp
│       │   ├── files
│       │   │   ├── Makefile
│       │   │   ├── nxp-simtemp.dtsi
│       │   │   └── simtemp.c
│       │   └── simtemp_1.0.bb
│       └── simtemp-cli
│           ├── files
│           │   └── simtemp_cli.py
│           └── simtemp-cli_1.0.bb
├── scripts
├── test
│   └── test_poll.c
└── user
    ├── cli
    │   └── cli_simtemp.py
    └── gui
```

---

##Detailed Documentation

For a full explanation of:

- Sysfs attributes  
- Device Tree mapping  
- Locking and synchronization  
- Scaling considerations  
- Kernel ↔ Userspace flow diagram  

See [`DESIGN.md`](./DESIGN.md)

---
