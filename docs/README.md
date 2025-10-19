
# **Simulated Temperature Sensor Linux Kernel Driver**

---

## Overview

`nxp_simtemp` is a **minimal Linux kernel driver** that simulates a temperature sensor device.  
It provides:

- A **misc device**: `/dev/simtemp`  
- **Sysfs attributes** for runtime configuration  
- A **Python CLI** (`simtemp_cli.py`) for user-space interaction and testing  

The driver periodically generates temperature samples using an **hrtimer**, allowing user-space to read them as if a real sensor were connected.

---

## Features

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
python3 cli_simtemp.py --sampling-ms 200 --threshold-mC 46000 --mode noisy
python3 cli_simtemp.py --show-sysfs
python3 cli_simtemp.py --test
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

### Make the build script executable and run

From the repository root:

```bash
# Make the script executable (one-time)
chmod +x scripts/build.sh

# Run the build script
./scripts/build.sh
```
The command `chmod +x scripts/build.sh` grants the script executable permission so it can be launched directly. Running `./scripts/build.sh` executes the script from the repository root; the script will verify kernel headers, build the kernel module, and optionally install Python dependencies for the CLI. Note: the script uses `set -e`, so it will stop immediately if any step fails.



### Demo Script: run_demo.sh

A helper script to exercise the module end-to-end: insert the kernel object, configure sysfs attributes (if present), run the user-space CLI test, and remove the module.

From the repository root:

```bash
# Make the demo script executable (one-time)
chmod +x scripts/run_demo.sh

# Run the demo
./scripts/run_demo.sh
```

What the script does:
- Verifies `kernel/nxp_simtemp.ko` exists (exit code 1 if missing).
- Inserts the module with `sudo insmod` (requires sudo — fail if insmod fails).
- Checks `/dev/simtemp` was created (exit code 2 if missing).
- Optionally writes sane values to sysfs attributes (sampling_ms, threshold_mC) if available.
- Runs the bundled CLI test `user/cli/cli_simtemp.py --test` (exit code 4 on test failure).
- Removes the module with `sudo rmmod` (exit code 3 if unload fails).
- Prints dmesg snippets and reports success/failure.

Notes:
- The script uses `set -e` so it aborts on the first error; inspect the printed messages for diagnostics.
- Commands that require elevated privileges (insmod, rmmod, writing sysfs) use `sudo`.
- If the CLI test requires Python deps, install them before running the demo:
  ```bash
  pip install -r user/cli/requirements.txt
  ```
- Typical return codes:
  - 0 — success
  - 1 — module file missing or insmod failed
  - 2 — device node not created
  - 3 — rmmod failed
  - 4 — CLI test failed


---

## Detailed Documentation

For a full explanation of:

- Sysfs attributes  
- Device Tree mapping  
- Locking and synchronization  
- Scaling considerations  
- Kernel ↔ Userspace flow diagram  

See [`DESIGN.md`](./DESIGN.md)

---
