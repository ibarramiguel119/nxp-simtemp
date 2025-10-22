#!/bin/bash
# Build script for NXP SimTemp kernel module + user CLI
set -e  # fail fast

echo "[INFO] Building NXP SimTemp kernel module and user app..."

# Go to repo root
ROOT_DIR=$(dirname "$(readlink -f "$0")")/..
cd "$ROOT_DIR"

# === Detect kernel headers ===
KDIR="/lib/modules/$(uname -r)/build"
if [ ! -d "$KDIR" ]; then
    echo "[ERROR] Kernel headers not found at $KDIR"
    echo "Please install them with:"
    echo "  sudo apt install linux-headers-$(uname -r)"
    exit 1
fi

# === Build kernel module ===
echo "[INFO] Compiling module against $(uname -r)"
make -C "$KDIR" M="$PWD/kernel" modules

# === Build / check user CLI ===
if [ -f "user/cli/simtemp_cli.py" ]; then
    echo "[INFO] Found user CLI: user/cli/simtemp_cli.py"
    # Optional: install Python requirements if exists
    if [ -f "user/cli/requirements.txt" ]; then
        echo "[INFO] Installing Python dependencies..."
        pip install -r user/cli/requirements.txt
    else
        echo "[INFO] No requirements.txt found; skipping dependency install."
    fi
else
    echo "[WARN] No user CLI found at user/cli/simtemp_cli.py"
fi

# === Done ===
echo
echo "[SUCCESS] Build completed successfully!"
echo "[INFO] Kernel module: kernel/nxp_simtemp.ko"
echo "[INFO] User CLI: user/cli/simptemp_cli.py"
