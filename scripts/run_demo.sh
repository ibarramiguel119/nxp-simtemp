#!/bin/bash
# Extended Demo Script for NXP SimTemp Kernel Module
# Performs: insmod → configure → test all modes (normal/noisy/ramp) → rmmod
# Returns non-zero on failure

set -e  # Abort on first failure

ROOT_DIR=$(dirname "$(readlink -f "$0")")/..
cd "$ROOT_DIR"

MOD="nxp_simtemp"
KO_PATH="kernel/${MOD}.ko"
DEV="/dev/simtemp"
CLI_PATH="user/cli/simtemp_cli.py"

echo "[INFO] === NXP SimTemp Extended Demo ==="

# --- Step 1: Check module exists ---
if [ ! -f "$KO_PATH" ]; then
    echo "[ERROR] Module file not found: $KO_PATH"
    echo "Run ./scripts/build.sh first."
    exit 1
fi

# --- Step 1.5: Remove module if already loaded ---
if lsmod | grep -q "^${MOD}"; then
    echo "[INFO] Module already loaded, removing first..."
    sudo rmmod "$MOD" || {
        echo "[ERROR] Failed to remove existing module."
        exit 1
    }
    sleep 1
fi

# --- Step 2: Insert module ---
echo "[INFO] Inserting kernel module..."
sudo insmod "$KO_PATH" || {
    echo "[ERROR] insmod failed"
    exit 1
}

sleep 1

# --- Step 3: Verify device node ---
if [ ! -e "$DEV" ]; then
    echo "[ERROR] Device node $DEV not found!"
    echo "[HINT] Check dmesg for probe errors."
    sudo rmmod "$MOD" || true
    exit 2
fi
echo "[OK] Device node created: $DEV"

# --- Step 4: Locate sysfs path ---
SYSFS_PATH=$(find /sys/class -type d -name "simtemp*" | head -n 1)
if [ -n "$SYSFS_PATH" ]; then
    echo "[OK] Sysfs path: $SYSFS_PATH"
else
    echo "[WARN] Sysfs attributes not found (expected for early versions)."
fi

# --- Step 5: Base Configuration ---
if [ -n "$SYSFS_PATH" ]; then
    echo "[INFO] Setting base configuration..."
    echo 200 | sudo tee "$SYSFS_PATH/sampling_ms" >/dev/null || true
    echo 45000 | sudo tee "$SYSFS_PATH/threshold_mC" >/dev/null || true
fi

echo
echo "[INFO] === Sysfs initial values ==="
if [ -n "$SYSFS_PATH" ]; then
    for attr in sampling_ms threshold_mC mode stats; do
        [ -f "$SYSFS_PATH/$attr" ] && echo "  $attr = $(cat $SYSFS_PATH/$attr)"
    done
fi

# --- Step 6: Show brief dmesg log ---
echo
echo "[INFO] Kernel log (last 10 lines):"
dmesg | tail -n 10

# --- Step 7: Run mode tests ---
if [ ! -f "$CLI_PATH" ]; then
    echo "[ERROR] CLI script not found at $CLI_PATH"
    sudo rmmod "$MOD" || true
    exit 3
fi

echo
echo "[INFO] === Running CLI configuration tests ==="

for mode in normal noisy ramp; do
    echo
    echo "[INFO] --- Testing mode: $mode ---"
    
    python3 "$CLI_PATH" --sampling-ms 200 --threshold-mC 46000 --mode "$mode" --show-sysfs

    echo "[INFO] Collecting 5 samples in mode: $mode"
    python3 "$CLI_PATH" --count 10
done

# --- Step 8: Run functional self-test ---
echo
echo "[INFO] Running CLI automated test (--test)"
python3 "$CLI_PATH" --test || {
    echo "[ERROR] CLI test failed!"
    sudo rmmod "$MOD" || true
    exit 4
}

# --- Step 9: Remove module ---
echo
echo "[INFO] Removing module..."
sudo rmmod "$MOD" || {
    echo "[ERROR] Failed to remove module."
    exit 5
}

sleep 1

# --- Step 10: Verify cleanup ---
if [ -e "$DEV" ]; then
    echo "[WARN] /dev/simtemp still exists after rmmod (check udev rules)."
else
    echo "[OK] Device node removed."
fi

echo
echo "[SUCCESS] All tests completed successfully!"
