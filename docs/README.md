
# **Simulated Temperature Sensor Linux Kernel Driver**

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Quick Start](#quick-start)
  - [Build and Load the Module](#1-build-and-load-the-module)
  - [Verify Device Node](#2-verify-device-node)
  - [Read Temperature Samples](#3-read-temperature-samples)
  - [Change Configuration](#4-change-configuration)
- [CLI Tool](#cli-tool)
- [Repository Structure](#repository-structure)
- [Build & Demo Scripts](#make-the-build-script-executable-and-run)
  - [build.sh](#build-sh)
  - [run_demo.sh](#demo-script-rundemosh)
- [Yocto Project Integration](#yocto-project-integration)
- [QEMU Deployment (Emulation)](#qemu-deployment-emulation)
- [Troubleshooting & Notes](#notes-and-troubleshooting)
- [Detailed Documentation](#detailed-documentation)


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
├── docs
│   ├── AI_NOTES.md
│   ├── DESIGN.md
│   ├── README.md
│   └── TESTPLAN.md
├── kernel
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
│       │   └── simtemp_1.0.bb
│       └── simtemp-cli
│           └── simtemp-cli_1.0.bb
├── scripts
│   ├── build.sh
│   └── run_demo.sh
├── test
│   └── test_poll.c
└── user
    ├── cli
    │   └── simtemp_cli.py
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
## Yocto Project Integration

### Prerequisites

- Host prepared for the i.MX Yocto BSP build (initialized build environment).
- Example target environment used in this guide:
  - MACHINE: imx8mp-lpddr4-evk
  - DISTRO: fsl-imx-xwayland
  - BSP root (example): ~/prueba_copilacion_yocto/imx-yocto-bsp

### 1. Environment and Layer Setup

The driver sources and Yocto recipes are in the repository's meta-simtemp layer.

From the BSP root sources directory:

```bash
cd imx-yocto-bsp/sources

# Clone the simtemp repository as a custom meta-layer
git clone --branch refactor/simtemp-unified-source \
    https://github.com/ibarramiguel119/nxp-simtemp.git meta-simtemp-test
```

Initialize the build environment (if not already done):

```bash
cd ..
MACHINE=imx8mp-lpddr4-evk DISTRO=fsl-imx-xwayland \
    source ./imx-setup-release.sh -b build
```

Add the new meta-layer to the build:

```bash
bitbake-layers add-layer sources/meta-simtemp-test/meta-simtemp
```
### 2. Configuration (build/conf/local.conf)

Edit `build/conf/local.conf` to install the driver and CLI into the image and include the DT overlay.

Recommended additions:

```conf
IMAGE_INSTALL += "simtemp simtemp-cli"
KERNEL_MODULE_AUTOLOAD += "simtemp"
UBOOT_OVERLAY = "simtemp.dtbo"
```

Purpose:
- IMAGE_INSTALL ensures the kernel module and Python CLI are added to the rootfs.
- KERNEL_MODULE_AUTOLOAD requests automatic loading of the module at boot.
- UBOOT_OVERLAY includes the Device Tree overlay for driver initialization.

### 3. Build and Deployment

Build the target image (this compiles the module and packages the CLI):

```bash
bitbake imx-image-core
```

After a successful build, deploy the generated image from:

```
build/tmp/deploy/images/<MACHINE>/
```

Follow your board-specific flashing steps to write the image and DTBO to the device.

Notes:
- Verify the layer and recipe names match your cloned repo layout (`meta-simtemp-test/meta-simtemp`).
- Adjust MACHINE/DISTRO values to your BSP configuration.
- If you modify recipes, run `bitbake -c clean <recipe>` before rebuilding.


## QEMU Deployment (Emulation)

These steps show how to unpack a built image, create a simple device-tree overlay that exposes the simulated simtemp device to the emulated machine, and run the image under QEMU.

Prerequisites:
- qemu-system-aarch64
- device-tree-compiler (dtc) and fdtoverlay (part of dtc tools)
- a built image file (example name shown below)

1. Uncompress the rootfs WIC image to a raw SD image:
```bash
unzstd imx-image-core-imx8mp-lpddr4-evk.rootfs-20251006055338.wic.zst -o sdcard.img
```

2. (Optional) Produce a base DTB from QEMU so you can apply an overlay:
```bash
qemu-system-aarch64 -M virt -machine dumpdtb=virt.dtb -nographic || true
# 'virt.dtb' will be created by QEMU; if not, supply a DTB from your BSP build.
```

3. Create overlay source file `simtemp-virt-overlay.dts` with the following contents:
```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "linux,dummy-virt";

    fragment@0 {
        target-path = "/";
        __overlay__ {
            simtemp@1000 {
                compatible = "nxp,simtemp";
                reg = <0x10000000 0x1000>;
                sampling-ms = <100>;
                threshold-mC = <45000>;
                status = "okay";
            };
        };
    };
};
```

4. Compile the overlay to DTBO:
```bash
mkdir -p overlays
dtc -@ -I dts -O dtb -o overlays/simtemp-virt.dtbo simtemp-virt-overlay.dts
```

5. Apply the overlay to the base DTB (produced above) to get a combined DTB:
```bash
fdtoverlay -i virt.dtb -o virt+simtemp.dtb -v overlays/simtemp-virt.dtbo
```

6. Launch QEMU with the kernel Image, the modified DTB and the sdcard image:
```bash
qemu-system-aarch64 \
  -M virt -cpu cortex-a53 -smp 4 -m 3G \
  -kernel Image \
  -dtb virt+simtemp.dtb \
  -append "root=/dev/vda2 rw console=ttyAMA0" \
  -drive file=sdcard.img,format=raw,if=virtio \
  -nographic
```

Notes and tips:
- Adjust filenames (Image, virt.dtb, sdcard.img) to match your BSP/build outputs.
- If `virt.dtb` was not generated by QEMU, use the DTB from your Yocto build (`build/tmp/deploy/images/<MACHINE>/`).
- Ensure dtc/fdtoverlay are installed (package name typically `device-tree-compiler`).
- The overlay above is minimal — adapt addresses and properties if your kernel driver or DTS expectations differ.
- Once the VM boots, verify the simtemp device and sysfs entries as with real hardware:
  ```bash
  ls -l /dev/simtemp
  ls -l /sys/class/misc/simtemp
  ```

Additional checks:
- The CLI may be installed system-wide as `/usr/bin/cli_simtemp.py` or similar; check `/usr/bin` in the guest to locate the executable.
- The live device-tree root is available at `/sys/firmware/devicetree/base` inside the running guest; you can inspect applied overlays and node properties there, for example:
  ```bash
  ls -l /sys/firmware/devicetree/base
  ```

## Detailed Documentation

For a full explanation of:

- Sysfs attributes  
- Device Tree mapping  
- Locking and synchronization  
- Scaling considerations  
- Kernel ↔ Userspace flow diagram  

See [`DESIGN.md`](./DESIGN.md)

---
