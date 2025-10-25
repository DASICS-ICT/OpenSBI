#!/bin/bash
# Build OpenSBI for XiangShan with correct device tree
set -e

# Switch to XiangShan/NoOP device tree
echo "[INFO] Switching to noop.dtsi for XiangShan build"
cd dts && rm -f platform.dtsi && ln -s noop.dtsi platform.dtsi && cd ..
echo "[INFO] platform.dtsi -> $(readlink dts/platform.dtsi)"

# Compile device tree
echo "[INFO] Compiling device tree"
dtc -O dtb -o system.dtb ./dts/system-qemu.dts 2>&1 | grep -v "Warning" || true

# Build OpenSBI with Linux payload
echo "[INFO] Building OpenSBI with Linux payload"
make PLATFORM=generic \
     CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     FW_FDT_PATH=system.dtb \
     FW_PAYLOAD_PATH=../riscv-linux/arch/riscv/boot/Image \
     -j$(nproc)

echo "[SUCCESS] OpenSBI built for XiangShan: build/platform/generic/firmware/fw_payload.bin"
