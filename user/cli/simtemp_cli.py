"""
simtemp_cli.py

User-space CLI to interact with /dev/simtemp and its sysfs attributes.
Features:
 - Read samples from /dev/simtemp (packed struct: u64 ts_ns; s32 temp_mC; u32 flags)
 - Print ISO8601 UTC timestamp, temperature (C) and alert flag
 - Configure sampling-ms, threshold_mC and mode via sysfs
 - Test mode: perform a small probabilistic test that tries to provoke a threshold
   crossing within 2 sampling periods (exits non-zero on failure)

Author: Miguel Elibert Ibarra Rodriguez 
"""

import argparse
import os
import sys
import struct
import time
import select
from datetime import datetime, timezone
import glob

# Packed C struct on kernel side: __u64 timestamp_ns; __s32 temp_mC; __u32 flags;
C_SAMPLE_FMT = "<Q i I"  # little-endian: unsigned long long, int, unsigned int
C_SAMPLE_SIZE = struct.calcsize(C_SAMPLE_FMT)

# Flags from kernel driver
SIMTEMP_FLAG_NEW_SAMPLE = (1 << 0)
SIMTEMP_FLAG_THRESHOLD_CROSSED = (1 << 1)

# Default device paths
DEV_PATH = "/dev/simtemp"
SYSFS_GLOB = "/sys/class/misc/simtemp*"


def find_sysfs_base():
    """Return the sysfs directory for the misc device (e.g. /sys/class/misc/simtemp)
       or None if not found.
    """
    matches = glob.glob(SYSFS_GLOB)
    if not matches:
        return None
    # Prefer exact match 
    for m in matches:
        if os.path.basename(m) == 'simtemp':
            return m
    return matches[0]


def write_sysfs_attr(attr, value, sysfs_base=None):
    if sysfs_base is None:
        sysfs_base = find_sysfs_base()
    if sysfs_base is None:
        raise FileNotFoundError("simtemp sysfs base not found under /sys/class/misc/")
    path = os.path.join(sysfs_base, attr)
    with open(path, 'w') as f:
        f.write(str(value))


def read_sysfs_attr(attr, sysfs_base=None):
    if sysfs_base is None:
        sysfs_base = find_sysfs_base()
    if sysfs_base is None:
        return None
    path = os.path.join(sysfs_base, attr)
    try:
        with open(path, 'r') as f:
            return f.read().strip()
    except Exception:
        return None


def format_sample(sample_tuple):
    ts_ns, temp_mC, flags = sample_tuple
    # Convert ns to UTC ISO8601 with millisecond precision
    ts_s = ts_ns / 1e9
    dt = datetime.fromtimestamp(ts_s, tz=timezone.utc)
    ts_str = dt.isoformat(timespec='milliseconds').replace('+00:00', 'Z')
    temp_c = temp_mC / 1000.0
    alert = 1 if (flags & SIMTEMP_FLAG_THRESHOLD_CROSSED) else 0
    return f"{ts_str} temp={temp_c:.3f}C alert={alert}", sample_tuple


def read_sample(fd):
    data = b''
    while len(data) < C_SAMPLE_SIZE:
        chunk = os.read(fd, C_SAMPLE_SIZE - len(data))
        if not chunk:
            raise EOFError('device closed')
        data += chunk
    return struct.unpack(C_SAMPLE_FMT, data)


def monitor(args):
    fd = os.open(args.dev, os.O_RDONLY)
    count = 0
    try:
        while args.count == 0 or count < args.count:
            data = os.read(fd, C_SAMPLE_SIZE)
            if not data:
                continue
            sample = struct.unpack(C_SAMPLE_FMT, data)
            line, _ = format_sample(sample)
            print(line)
            count += 1
    finally:
        os.close(fd)
    return 0


def do_config_set(args):
    sysfs = find_sysfs_base()
    if sysfs is None:
        print("simtemp sysfs not found; ensure module is loaded", file=sys.stderr)
        return 2

    if args.sampling_ms is not None:
        try:
            write_sysfs_attr('sampling_ms', int(args.sampling_ms), sysfs)
            print(f"sampling_ms -> {args.sampling_ms} ms")
        except Exception as e:
            print(f"failed to set sampling_ms: {e}", file=sys.stderr)
            return 3

    if args.threshold_mC is not None:
        try:
            write_sysfs_attr('threshold_mC', int(args.threshold_mC), sysfs)
            print(f"threshold_mC -> {args.threshold_mC} mC")
        except Exception as e:
            print(f"failed to set threshold_mC: {e}", file=sys.stderr)
            return 3

    if args.mode is not None:
        try:
            write_sysfs_attr('mode', args.mode, sysfs)
            print(f"mode -> {args.mode}")
        except Exception as e:
            print(f"failed to set mode: {e}", file=sys.stderr)
            return 3

    return 0


def parse_args():
    p = argparse.ArgumentParser(description='CLI for /dev/simtemp')
    p.add_argument('--dev', default=DEV_PATH, help='device path (default: /dev/simtemp)')
    p.add_argument('--nonblock', action='store_true', help='open device non-blocking')
    p.add_argument('--count', type=int, default=0, help='number of samples to print (0 = infinite)')
    p.add_argument('--timeout-ms', type=int, default=None, help='poll timeout in ms (default none)')

    cfg = p.add_argument_group('configure')
    cfg.add_argument('--sampling-ms', type=int, help='set sampling period (ms) via sysfs')
    cfg.add_argument('--threshold-mC', type=int, help='set threshold (milli-degrees C) via sysfs')
    cfg.add_argument('--mode', choices=['normal', 'noisy', 'ramp'], help='set mode via sysfs')

    p.add_argument('--test', action='store_true', help='run automated test (exit non-zero on failure)')
    p.add_argument('--show-sysfs', action='store_true', help='print sysfs attributes and exit')

    return p.parse_args()


def read_one_sample_blocking(devpath):
    fd = os.open(devpath, os.O_RDONLY)
    try:
        data = os.read(fd, C_SAMPLE_SIZE)
        return struct.unpack(C_SAMPLE_FMT, data)
    finally:
        os.close(fd)


def run_test(args):
    """Test strategy (best-effort):
       1) Read a baseline sample (blocking)
       2) Switch mode to 'noisy' to increase chance of crossing
       3) Set threshold to baseline_temp_mC + 1
       4) Poll for up to 2 * sampling_ms for the next sample and check alert flag
    """
    sysfs = find_sysfs_base()
    if sysfs is None:
        print("simtemp sysfs not found; ensure module is loaded", file=sys.stderr)
        return 2

    # get sampling_ms for timeout calculation
    sampling_ms_s = read_sysfs_attr('sampling_ms', sysfs)
    try:
        sampling_ms = int(float(sampling_ms_s)) if sampling_ms_s else 100
    except Exception:
        sampling_ms = 100

    print(f"reading baseline sample from {args.dev}...")
    try:
        baseline = read_one_sample_blocking(args.dev)
    except Exception as e:
        print(f"failed to read baseline sample: {e}", file=sys.stderr)
        return 3

    _, temp_mC, _ = baseline
    print(f"baseline temp {temp_mC} mC")

    # set mode noisy
    try:
        write_sysfs_attr('mode', 'noisy', sysfs)
    except Exception as e:
        print(f"failed to set mode: {e}", file=sys.stderr)

    # set threshold to baseline + 1 to maximize chance of crossing
    try:
        new_threshold = temp_mC + 1
        write_sysfs_attr('threshold_mC', new_threshold, sysfs)
        print(f"temporary threshold set to {new_threshold} mC")
    except Exception as e:
        print(f"failed to set threshold: {e}", file=sys.stderr)
        return 4

    # Poll for next sample (2 periods + 200ms margin)
    timeout_ms = 10 * sampling_ms
    print(f"waiting up to {timeout_ms} ms for an alert (2 periods)...")

    fd = os.open(args.dev, os.O_RDONLY | os.O_NONBLOCK)
    poll = select.poll()
    poll.register(fd, select.POLLIN)

    start = time.time()
    got_alert = False
    try:
        events = poll.poll(timeout_ms)
        if events:
            try:
                sample = read_sample(fd)
            except Exception as e:
                print(f"read error during test: {e}", file=sys.stderr)
                return 5
            flags = sample[2]
            if flags & SIMTEMP_FLAG_THRESHOLD_CROSSED:
                print("TEST: alert observed - success")
                got_alert = True
            else:
                print("TEST: sample received but no alert flag set")
        else:
            print("TEST: timeout waiting for sample")
    finally:
        os.close(fd)

    if not got_alert:
        print("TEST: FAILED (no alert within 2 periods)")
        return 10
    return 0


def main():
    args = parse_args()

    if args.show_sysfs:
        base = find_sysfs_base()
        if not base:
            print("sysfs base not found")
            return 2
        for a in ['sampling_ms', 'threshold_mC', 'mode', 'stats']:
            v = read_sysfs_attr(a, base)
            print(f"{a}: {v}")
        return 0

    # Do any configuration requested
    if args.sampling_ms is not None or args.threshold_mC is not None or args.mode is not None:
        ret = do_config_set(args)
        if ret != 0:
            return ret

    if args.test:
        return run_test(args)

    return monitor(args)


if __name__ == '__main__':
    sys.exit(main())
