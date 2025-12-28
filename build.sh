#!/bin/bash

echo "[INFO] Waiting 5 seconds before starting..."
sleep 5

set -e

############################################
# CONFIGURATION
############################################

# Matrix (original) source directory
MATRIX_SRC="/home/chanz22/Documentos/GitHub/ASUS_ZenFone_ZB551KL"

# RAM build directory (tmpfs)
RAM_BASE="/dev/shm"
RAM_SRC="$RAM_BASE/kernel_build_ram"

# Output directory name
OUT_DIR="out"

# Defconfig
DEFCONFIG="zc551kl_defconfig"

# Parallel jobs
JOBS=$(nproc)

############################################
# PRE-FLIGHT CHECKS
############################################

echo "[INFO] Starting RAM-based kernel build..."

# Check if matrix source exists
if [ ! -d "$MATRIX_SRC" ]; then
    echo "[ERROR] Matrix source directory not found: $MATRIX_SRC"
    exit 1
fi

# Check if RAM base exists
if [ ! -d "$RAM_BASE" ]; then
    echo "[ERROR] RAM base directory does not exist: $RAM_BASE"
    exit 1
fi

# Warn about RAM usage
echo "[WARNING] This build will copy the entire kernel source into RAM."
echo "[WARNING] Make sure you have enough free memory."

############################################
# CLEAN PREVIOUS RAM BUILD
############################################

if [ -d "$RAM_SRC" ]; then
    echo "[INFO] Removing previous RAM build directory..."
    rm -rf "$RAM_SRC"
fi

############################################
# COPY SOURCE TO RAM
############################################

echo "[INFO] Copying kernel source to RAM..."
cp -a "$MATRIX_SRC" "$RAM_SRC"

if [ ! -d "$RAM_SRC" ]; then
    echo "[ERROR] Failed to copy source to RAM."
    exit 1
fi

############################################
# ENTER RAM SOURCE
############################################

cd "$RAM_SRC" || {
    echo "[ERROR] Failed to enter RAM source directory."
    exit 1
}

############################################
# CLEAN OUT DIRECTORY IN RAM
############################################

if [ -d "$OUT_DIR" ]; then
    echo "[INFO] Removing old out directory in RAM source..."
    rm -rf "$OUT_DIR"
fi

mkdir "$OUT_DIR"

############################################
# BUILD PROCESS
############################################

echo "[INFO] Running defconfig: $DEFCONFIG"
make O="$OUT_DIR" "$DEFCONFIG"

echo "[INFO] Building kernel with $JOBS parallel jobs..."
make O="$OUT_DIR" -j"$JOBS"

############################################
# COPY OUT BACK TO MATRIX SOURCE
############################################

echo "[INFO] Copying build output back to matrix source..."

# Remove old out in matrix source
if [ -d "$MATRIX_SRC/$OUT_DIR" ]; then
    echo "[INFO] Removing old out directory in matrix source..."
    rm -rf "$MATRIX_SRC/$OUT_DIR"
fi

# Copy new out
cp -a "$OUT_DIR" "$MATRIX_SRC/"

############################################
# FINAL CHECKS
############################################

if [ ! -d "$MATRIX_SRC/$OUT_DIR" ]; then
    echo "[ERROR] Failed to copy out directory back to matrix source."
    exit 1
fi

echo "[SUCCESS] Kernel build completed successfully."
echo "[INFO] Output directory is available at:"
echo "       $MATRIX_SRC/$OUT_DIR"

############################################
# OPTIONAL CLEANUP
############################################
# Uncomment if you want automatic RAM cleanup
# rm -rf "$RAM_SRC"

