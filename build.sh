#!/bin/bash

############################################
# START DELAY
############################################
echo "[INFO] Waiting 5 seconds before starting..."
sleep 5

############################################
# SAFETY
############################################
set -e

START_TIME=$(date +%s)

############################################
# PATHS
############################################

MATRIX_SRC="/home/chanz22/Documentos/GitHub/ASUS_ZenFone_ZB551KL"
RAM_SRC="/dev/shm/kernel_build_ram"
OUT_DIR="out"
DEFCONFIG="zc551kl_defconfig"
JOBS=$(nproc)

TEMP_DIR="$MATRIX_SRC/temp"
AIK_DIR="$MATRIX_SRC/AIK"
IMAGE_OUT_DIR="$MATRIX_SRC/image_output"
BOOT_ARCH_DIR="$MATRIX_SRC/out/arch/arm/boot"

############################################
# PRE-FLIGHT CHECKS
############################################

echo "[INFO] Checking environment..."

[ -d "$MATRIX_SRC" ] || { echo "[ERROR] Matrix source not found"; exit 1; }
[ -d "$AIK_DIR" ] || { echo "[ERROR] AIK directory not found"; exit 1; }
command -v dtbTool >/dev/null || { echo "[ERROR] dtbTool not found in PATH"; exit 1; }
command -v fastboot >/dev/null || { echo "[ERROR] fastboot not found in PATH"; exit 1; }


############################################
# GLOBAL CLEANUP FUNCTION
############################################

cleanup_workspace() {
    echo "[INFO] Performing global cleanup before build..."
    
    
    cd "$AIK_DIR"

    ./cleanup.sh
    
    cd "$MATRIX_SRC"

    # Remove previous RAM build
    rm -rf "$RAM_SRC"

    # Clean kernel out directory
    rm -rf "$MATRIX_SRC/out"

    # Clean temporary working directories
    rm -rf "$MATRIX_SRC/temp"
    rm -rf "$MATRIX_SRC/image_output"

    echo "[INFO] Cleanup completed."
}

############################################
# PREPARE RAM BUILD
############################################

echo "[INFO] Preparing RAM build directory..."

cleanup_workspace

rm -rf "$RAM_SRC"
cp -a "$MATRIX_SRC" "$RAM_SRC"

cd "$RAM_SRC"

############################################
# KERNEL BUILD (RAM)
############################################

echo "[INFO] Starting kernel build in RAM..."

rm -rf "$OUT_DIR"
mkdir "$OUT_DIR"

make O="$OUT_DIR" "$DEFCONFIG"
make O="$OUT_DIR" -j"$JOBS"

############################################
# SYNC OUT BACK TO MATRIX
############################################

echo "[INFO] Syncing build output back to matrix source..."

rm -rf "$MATRIX_SRC/$OUT_DIR"
cp -a "$OUT_DIR" "$MATRIX_SRC/"

############################################
# COLLECT BOOT FILES
############################################

echo "[INFO] Collecting kernel artifacts..."

mkdir -p "$TEMP_DIR"
cd "$BOOT_ARCH_DIR"

DTBS=(
  msm8926-v1-720p-mtp-zc551kl-pr.dtb
  msm8926-v1-720p-mtp-zc551kl-er1.dtb
  msm8926-v1-720p-mtp-zc551kl-er2.dtb
)

for dtb in "${DTBS[@]}"; do
    [ -f "$dtb" ] || { echo "[ERROR] Missing DTB: $dtb"; exit 1; }
    mv "$dtb" "$TEMP_DIR/"
done

[ -f Image ] || { echo "[ERROR] Image not found"; exit 1; }
mv Image "$TEMP_DIR/"

############################################
# DTB COMBINATION
############################################

cd "$TEMP_DIR"

mkdir kernel_img
mv Image kernel_img/

echo "[INFO] Combining DTBs with dtbTool..."
dtbTool -o boot.img-dt -s 2048

rm -f *.dtb

mv kernel_img/Image .
rmdir kernel_img

############################################
# MOVE TO AIK
############################################

echo "[INFO] Moving kernel artifacts to AIK..."

mv boot.img-dt Image "$AIK_DIR/"
cd "$AIK_DIR"

############################################
# AIK PROCESS
############################################

echo "[INFO] Running AIK cleanup..."
./cleanup.sh

echo "[INFO] Unpacking boot image..."
./unpackimg.sh

############################################
# REPLACE KERNEL + DTB
############################################

cd split_img
rm -f boot.img-dt boot.img-kernel
cd ..

mv Image boot.img-kernel
cp boot.img-kernel split_img/
cp boot.img-dt split_img/

############################################
# REPACK BOOT IMAGE
############################################

echo "[INFO] Repacking boot image..."
./repackimg.sh

############################################
# FINALIZE IMAGE
############################################

mv image-new.img bimbo_kernel.img
rm -f boot.img-dt boot.img-kernel

############################################
# CLEAN & EXPORT
############################################

cd "$MATRIX_SRC"

rm -rf temp
rm -rf "$IMAGE_OUT_DIR"
mkdir "$IMAGE_OUT_DIR"

mv "$AIK_DIR/bimbo_kernel.img" "$IMAGE_OUT_DIR/"

############################################
# BUILD DONE
############################################

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo -e "\e[32m##################################"
echo -e "#  [INFO] : kernel build success!"
echo -e "#  Time elapsed : ${ELAPSED}s"
echo -e "##################################\e[0m"

############################################
# OPTIONAL FASTBOOT FLASH
############################################

echo
read -p "[QUESTION] Do you want to flash the image to device? [Y/n]: " FLASH_CHOICE

case "$FLASH_CHOICE" in
    n|N)
        echo -e "\e[33m############################"
        echo -e "# Flash to device skipped... script exiting"
        echo -e "############################\e[0m"
        exit 0
        ;;
    y|Y|"")
        ;;
    *)
        echo "[ERROR] Invalid option. Exiting."
        exit 1
        ;;
esac

############################################
# WAIT FOR FASTBOOT DEVICE
############################################

SECONDS_WAITED=0

echo -e "\e[33m#############################################"
echo -e "# [INFO] : Waiting for device in fastboot..."
echo -e "#############################################\e[0m"

while true; do
    if fastboot devices | grep -q fastboot; then
        break
    fi
    sleep 1
    ((SECONDS_WAITED++))
    echo -ne "\r[INFO] Waiting... ${SECONDS_WAITED}s"
done
echo

############################################
# DEVICE DETECTED
############################################

DEVICE_INFO=$(fastboot oem device-info 2>&1)

echo -e "\e[32m################################"
echo -e "# Device detected. flashing now...."
echo -e "################################\e[0m"

echo -e "\e[32m#############################################"
echo -e "# [INFO] : Device info..."
echo -e "#############################################\e[0m"
echo -e "$DEVICE_INFO"

############################################
# FLASH IMAGE
############################################

cd "$IMAGE_OUT_DIR"

fastboot flash boot bimbo_kernel.img

############################################
# REBOOT OPTION
############################################

echo
read -p "[QUESTION] Image flashed to device! Do you want reboot? [Y/n]: " REBOOT_CHOICE

case "$REBOOT_CHOICE" in
    y|Y|"")
        echo -e "\e[33m###################################"
        echo -e "# rebooting device..."
        echo -e "###################################\e[0m"
        fastboot reboot
        ;;
    n|N)
        echo -e "\e[33m###################################"
        echo -e "# Reboot skipped... exiting script now"
        echo -e "###################################\e[0m"
        ;;
    *)
        echo "[ERROR] Invalid option. Exiting."
        exit 1
        ;;
esac
