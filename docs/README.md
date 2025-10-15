# nxp_simtemp Driver Documentation

## 1. Overview

`nxp_simtemp` is a **minimal Linux kernel driver** that simulates a temperature sensor.  
It provides:

- A **misc device** `/dev/simtemp` for reading temperature samples.  
- **Sysfs attributes** for configuring sampling parameters, alert threshold, and operating mode.  
- A **user-space CLI (`simtemp_cli.py`)** to interact with the device and perform automated tests.

The driver generates periodic temperature samples using an **hrtimer**, delivering them to user-space via **blocking reads** or **poll**.

---

## 2. Purpose

The main purposes of the driver are:

1. **Simulate temperature sensors** for software testing without real hardware.  
2. Provide a **stable and packaged interface** (`struct simtemp_sample`) for user-space consumption.  
3. Enable **dynamic configuration** of:
   - Sampling interval (`sampling_ms`)  
   - Alert threshold (`threshold_mC`)  
   - Operating mode (`normal`, `noisy`, `ramp`)  
4. Allow **automated testing** via the CLI to validate alert detection and data consistency.  

---

## 3. Components

### Kernel Driver (`nxp_simtemp.c`)

- Sample ring buffer (64 elements)  
- Hrtimer for periodic sampling  
- Sysfs attributes for configuration and statistics  
- File operations (`read`, `poll`, `open/release`)  

### User-space CLI (`simtemp_cli.py`)

- Read samples and format output (ISO8601, °C, alerts)  
- Configure parameters via sysfs  
- Probabilistic alert testing  

---

## 4. Sysfs Attributes

| Attribute       | Type   | RW | Description |
|-----------------|--------|----|-------------|
| `sampling_ms`   | u64    | RW | Sampling interval in ms (hrtimer). |
| `threshold_mC`  | int    | RW | Temperature threshold for alert. |
| `mode`          | enum   | RW | Operating mode: `normal`, `noisy`, `ramp`. |
| `stats`         | struct | RO | Statistics: `updates`, `alerts`, `last_error`. |

**Examples:**
```bash
# Read sampling interval
cat /sys/class/misc/simtemp/sampling_ms

# Change to 200 ms
echo 200 > /sys/class/misc/simtemp/sampling_ms

# Read current mode
cat /sys/class/misc/simtemp/mode

# Change mode to "noisy"
echo noisy > /sys/class/misc/simtemp/mode

# View statistics
cat /sys/class/misc/simtemp/stats
```

---

## 5. Device Tree (DT) Mapping

If **Device Tree** is used, initial values for the `nxp_simtemp` driver can be configured.  
This allows setting sampling parameters and alert threshold without modifying code or using sysfs.

### Device Tree Example

```dts
nxp_simtemp@0 {
    compatible = "nxp,simtemp";
    sampling-ms = <100>;        // Sampling interval in milliseconds
    threshold-mC = <45000>;     // Temperature threshold in millidegrees Celsius (45°C)
};
```

If no DT is available, the driver registers a **fake platform device** with default parameters:

| Parameter | Default |
|------------|----------|
| Sampling interval | 100 ms |
| Threshold | 45000 mC |
| Mode | normal |

---

## 6. Operating Modes

| Mode     | Description |
|----------|-------------|
| `normal` | Base temperature ± 250 mC. |
| `noisy`  | Base ± 250 mC plus jitter ± 1000 mC. |
| `ramp`   | Linear repetitive increment from 30°C to 80°C. |

> These modes can be configured via **sysfs** (`/sys/class/misc/simtemp/mode`) or using the **CLI** with the `--mode` option.

---

## 7. CLI Usage

The CLI `simtemp_cli.py` allows interaction with `/dev/simtemp` and driver configuration from user-space.

### Main Commands

| Option                | Description |
|-----------------------|-------------|
| `--dev <path>`        | Device path (default `/dev/simtemp`). |
| `--count <n>`         | Number of samples to read (0 = infinite). |
| `--sampling-ms <n>`   | Set sampling period via sysfs. |
| `--threshold-mC <n>`  | Set alert threshold via sysfs. |
| `--mode <mode>`       | Set mode (`normal`, `noisy`, `ramp`). |
| `--test`              | Run probabilistic alert test. |
| `--show-sysfs`        | Show current sysfs attributes. |

### Usage Examples

```bash
# Read infinite samples
python3 simtemp_cli.py --dev /dev/simtemp

# Read 5 samples
python3 simtemp_cli.py --count 5

# Configure parameters
python3 simtemp_cli.py --sampling-ms 200 --threshold-mC 46000 --mode noisy

# Show sysfs attributes
python3 simtemp_cli.py --show-sysfs

# Run alert test
python3 simtemp_cli.py --test
```

---

## 8. Kernel ↔ Userspace Data Flow

```
 ┌──────────────────────────┐
 │     User-Space CLI       │
 │    simtemp_cli.py        │
 │ ┌──────────────────────┐ │
 │ │ Sysfs config (/sys/) │ │ ←─── Configure sampling, threshold, mode
 │ └──────────────────────┘ │
 │ ┌──────────────────────┐ │
 │ │ Read / Poll /dev/... │ │ ←─── Get samples
 │ └──────────────────────┘ │
 └───────────┬──────────────┘
             │
             │ read()/poll()
             ▼
 ┌──────────────────────────┐
 │     Kernel Driver        │
 │     nxp_simtemp.c        │
 │                          │
 │ [hrtimer] → produce_temperature_mC()
 │       │
 │       ▼
 │  Ring buffer (mutex protected)
 │       │
 │       └── wake_up_interruptible() → user-space
 └──────────────────────────┘
```

> **Figure:** Data and control flow between the `simtemp_cli.py` user-space utility and the `nxp_simtemp` kernel driver.

---

## 9. Problem-Solving Write-Up

### Locking Choices

- A single **mutex** (`s->lock`) protects buffer indices and sample data.  
- Used in both the **hrtimer callback** and the **read()** function.  
- Spinlocks are **not required** since the callback context allows sleeping and critical sections are short.  
- **Wait queues** (`wait_event_interruptible`) handle blocking reads and synchronization with user-space.

---

### API Trade-Offs

- **Sysfs** is used for configuration because it’s standard, simple, and human-readable.  
- **`read()`/`poll()`** are used for streaming data (binary `struct simtemp_sample`).  
- **`ioctl()`** is intentionally unimplemented to keep the API minimal and aligned with kernel conventions.

---

### Device Tree Mapping

- `compatible = "nxp,simtemp"` enables driver binding.  
- Optional DT properties:
  - `sampling-ms` → sets sampling interval.  
  - `threshold-mC` → sets temperature threshold.  
- If no DT exists, defaults are applied (`100 ms`, `45000 mC`), and a **test platform device** is registered automatically.

---

### Scaling (10 kHz Sampling Scenario)

At high sampling rates (10 kHz → 100 µs period), the following limitations appear:

| Component | Limitation | Effect |
|------------|-------------|--------|
| **hrtimer** | Scheduling jitter > 100 µs | Missed samples |
| **mutex lock** | High contention | Timing instability |
| **wake_up_interruptible()** | 10,000 wakeups/sec | CPU overload |
| **ring buffer (64)** | Overflow | Lost data |

#### Mitigation Strategies

- Batch multiple samples before signaling user-space.  
- Replace ring buffer + mutex with **kfifo** + spinlock (faster).  
- Use a **workqueue** or **threaded IRQ** to defer user wakeups.  
- Increase buffer depth.  
- For production-grade performance, migrate to **IIO framework**, which supports efficient buffered sampling.

---

