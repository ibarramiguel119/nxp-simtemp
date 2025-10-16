#!/bin/bash
# Demo script for NXP SimTemp kernel module
# Performs: insmod → configure → run CLI test → rmmod
# Returns non-zero on failure

set -e  # abort on first failure

ROOT_DIR=$(dirname "$(readlink -f "$0")")/..
cd "$ROOT_DIR"

MOD="nxp_simtemp"
KO_PATH="kernel/${MOD}.ko"
DEV="/dev/simtemp"

echo "[INFO] === NXP SimTemp Demo ==="

# --- Step 1: Check module exists ---
if [ ! -f "$KO_PATH" ]; then
    echo "[ERROR] Module file not found: $KO_PATH"
    echo "Run ./scripts/build.sh first."
    exit 1
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

# --- Step 4: Try to locate sysfs path ---
SYSFS_PATH=$(find /sys/class -type d -name "simtemp*" | head -n 1)
if [ -n "$SYSFS_PATH" ]; then
    echo "[OK] Sysfs path: $SYSFS_PATH"
else
    echo "[WARN] Sysfs attributes not found (expected for early versions)."
fi

# --- Step 5: Configure (if sysfs available) ---
if [ -n "$SYSFS_PATH" ]; then
    if [ -f "$SYSFS_PATH/sampling_ms" ]; then
        echo "[INFO] Setting sampling period to 200ms"
        echo 200 | sudo tee "$SYSFS_PATH/sampling_ms" >/dev/null
    fi
    if [ -f "$SYSFS_PATH/threshold_mC" ]; then
        echo "[INFO] Setting threshold to 45000 m°C"
        echo 45000 | sudo tee "$SYSFS_PATH/threshold_mC" >/dev/null
    fi

    echo "[INFO] Current sysfs values:"
    for attr in sampling_ms threshold_mC mode stats; do
        [ -f "$SYSFS_PATH/$attr" ] && echo "  $attr = $(cat $SYSFS_PATH/$attr)"
    done
fi

# --- Step 6: Show brief dmesg log ---
echo
echo "[INFO] Kernel log (last 10 lines):"
dmesg | tail -n 10

# --- Step 6.5: Run CLI test ---
CLI_PATH="user/cli/cli_simtemp.py"
if [ -f "$CLI_PATH" ]; then
    echo
    echo "[INFO] Running CLI test: $CLI_PATH"
    python3 "$CLI_PATH" --test || {
        echo "[ERROR] CLI test failed!"
        sudo rmmod "$MOD" || true
        exit 4
    }
else
    echo "[WARN] CLI test not found at $CLI_PATH (skipping user-space test)."
fi

# --- Step 7: Unload module ---
echo "[INFO] Removing module..."
sudo rmmod "$MOD" || {
    echo "[ERROR] Failed to remove module."
    exit 3
}

sleep 1

# --- Step 8: Verify cleanup ---
if [ -e "$DEV" ]; then
    echo "[WARN] /dev/simtemp still exists after rmmod (check udev rules)."
else
    echo "[OK] Device node removed."
fi

echo
echo "[SUCCESS] Demo completed successfully!"
