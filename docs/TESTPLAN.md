# Test Plan — nxp_simtemp

## Purpose
Define acceptance and verification tests for the nxp_simtemp kernel module and user CLI. Tests cover build, runtime behavior, sysfs controls, async notification (`poll`), CLI functionality and emulated deployment (QEMU / Yocto).

## Test environment / prerequisites
- Host: Linux with matching kernel headers: `sudo apt install linux-headers-$(uname -r) build-essential`
- Python 3 (optional venv) and pip
- qemu-system-aarch64, dtc (device-tree-compiler), fdtoverlay (for QEMU test)
- Repository checked out; run tests from repo root
- Module path: `kernel/nxp_simtemp.ko`
- CLI path: `user/cli/cli_simtemp.py` (normalize name in repo if needed)

## General notes
- Many actions require sudo (insmod, rmmod, writing sysfs).
- `cat /dev/simtemp` returns binary packed samples — inspect with `hexdump -C`.
- CLI test exits with non-zero on failure; see exit codes below.

## Test cases

### 1 — Build
Objective: module compiles against current kernel.
Steps:
1. From repo root:
   - `chmod +x scripts/build.sh`
   - `./scripts/build.sh`
Expected:
- `kernel/nxp_simtemp.ko` created.
Pass criteria: build script exits 0, no compile errors.

### 2 — Load / Unload
Objective: module loads and cleans up device node.
Steps:
1. `sudo insmod kernel/nxp_simtemp.ko`
2. Wait 1s; verify:
   - `dmesg | tail -n 20 | grep -i simtemp`
   - `ls -l /dev/simtemp`
3. `sudo rmmod nxp_simtemp`
Expected:
- Device node appears after insmod and is removed after rmmod.
Pass: device node present after insmod; removed after rmmod.

### 3 — Sysfs attributes (RW)
Objective: confirm sysfs controls exist and respond.
Steps:
1. Locate base: `ls /sys/class/misc/simtemp*`
2. Read attributes: `cat sampling_ms threshold_mC mode stats`
3. Set attributes:
   - `echo 200 | sudo tee sampling_ms`
   - `echo 45000 | sudo tee threshold_mC`
   - `echo noisy | sudo tee mode`
Expected:
- Writes succeed; subsequent reads reflect new values.
Pass: values change and persist until changed again.

### 4 — Read sample format
Objective: verify binary sample structure and contents.
Steps:
1. `dd if=/dev/simtemp bs=16 count=1 2>/dev/null | hexdump -C`
2. Interpret bytes as struct: `<Q i I` (timestamp_ns, temp_mC, flags)
3. Use CLI `python3 user/cli/cli_simtemp.py --count 1` to see formatted output.
Expected:
- Timestamp plausible; temp_mC reasonable; flags bit0 set for new sample.
Pass: sample unpacks to expected types; CLI shows ISO8601 timestamp.

### 5 — Poll / Async notification
Objective: confirm `poll()` notifies on new samples.
Steps:
1. Use provided `test/test_poll.c` or CLI in non-blocking mode with poll.
2. Set sampling_ms to a short period (e.g., 100 ms).
3. Poll for events and ensure a sample arrives within expected window.
Expected:
- Poll returns POLLIN when sample available.
Pass: poll unblocks and sample read successfully.

### 6 — CLI configuration commands
Objective: confirm CLI can set sysfs attributes.
Steps:
- `python3 user/cli/cli_simtemp.py --sampling-ms 200 --threshold-mC 46000 --mode noisy`
- Verify with `python3 user/cli/cli_simtemp.py --show-sysfs` or `cat` sysfs files.
Expected:
- Values set and reflected by sysfs.
Pass: CLI returns 0 and sysfs reflects settings.

### 7 — CLI automated test (`--test`)
Objective: validate alert detection flow implemented in `run_test`.
Steps:
1. Ensure module loaded and CLI present.
2. `python3 user/cli/cli_simtemp.py --test`
Expected:
- CLI reads baseline, sets temporary threshold (baseline + 1), polls up to timeout and reports success or failure.
Pass: CLI exits 0 and prints "TEST: alert observed - success".
Failure codes (examples from script):
- 2 = sysfs not found
- 3 = failed to read baseline
- 4 = failed to set threshold
- 5 = read error during test
- 10 = no alert within timeout

### 8 — Demo script
Objective: end-to-end exercise: insmod → configure → test → rmmod
Steps:
- `chmod +x scripts/run_demo.sh`
- `./scripts/run_demo.sh`
Expected:
- Script completes with exit 0; module inserted, CLI test passed, module removed.
Pass: script prints `[SUCCESS] Demo completed successfully!`

### 9 — QEMU emulation test
Objective: boot image in QEMU with simtemp overlay and validate device appears.
Steps (high level):
1. Decompress image: `unzstd <image>.wic.zst -o sdcard.img`
2. Generate/obtain `virt.dtb` (e.g. `qemu-system-aarch64 -M virt -machine dumpdtb=virt.dtb -nographic`)
3. Create `simtemp-virt-overlay.dts` (see README), compile to DTBO:
   - `dtc -@ -I dts -O dtb -o overlays/simtemp-virt.dtbo simtemp-virt-overlay.dts`
   - `fdtoverlay -i virt.dtb -o virt+simtemp.dtb -v overlays/simtemp-virt.dtbo`
4. Boot QEMU:
   ```
   qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 4 -m 3G \
     -kernel Image \
     -dtb virt+simtemp.dtb \
     -append "root=/dev/vda2 rw console=ttyAMA0" \
     -drive file=sdcard.img,format=raw,if=virtio \
     -nographic
   ```
5. Inside guest verify:
   - `/dev/simtemp` exists
   - `/sys/class/misc/simtemp` and `/sys/firmware/devicetree/base` show simtemp node
Expected:
- Device present in guest, sysfs attributes accessible.
Pass: verification commands succeed.

### 10 — Yocto integration test
Objective: confirm recipes add module + CLI to image and DTBO included.
Steps:
1. Add layer and configure `local.conf` as documented.
2. `bitbake imx-image-core`
3. Inspect `build/tmp/deploy/images/<MACHINE>/` for generated `.dtbo` and image.
4. Boot target and verify device and CLI present (or in `/usr/bin`).
Expected:
- Image contains `simtemp` module and `simtemp-cli`, DT overlay present.
Pass: module and CLI available on target; module autoloads if configured.

## Cleanup steps
- Remove module: `sudo rmmod nxp_simtemp || true`
- Clean kernel build: `make -C /lib/modules/$(uname -r)/build M="$PWD/kernel" clean`
- Remove temporary images/DTBOs created for QEMU as needed.

## Acceptance criteria
- All mandatory tests (Build, Load/Unload, Sysfs RW, Read format, CLI test) pass.
- Demo script completes successfully.
- Emulation and Yocto flows produce a bootable image with simtemp exposed.

## Troubleshooting pointers
- Kernel headers missing → install `linux-headers-$(uname -r)`.
- `/dev/simtemp` missing → check `dmesg` for probe errors; confirm module exported misc device.
- CLI failures → ensure correct sysfs path (`/sys/class/misc/simtemp*`) and permissions.
- QEMU DTB missing → use DTB from Yocto deploy images if `dumpdtb` failed.

## Automation / CI suggestions
- Add CI job to run `./scripts/build.sh` and basic `insmod`/`rmmod` checks in a kernel-compatible runner.
- For QEMU boot tests, use headless QEMU and an automated expect/script to verify sysfs presence.

# End of Test Plan