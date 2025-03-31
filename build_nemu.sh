export PATH=/data/jinyue/riscv-toolchain/nanhu-v10.0.2/bin:$PATH
export RISCV=/data/jinyue/riscv-toolchain/nanhu-v10.0.2
dtc -O dtb -o system.dtb ./dts/system-qemu.dts
make PLATFORM=generic CROSS_COMPILE=riscv64-unknown-linux-gnu- FW_FDT_PATH=system.dtb FW_PAYLOAD_PATH=../riscv-linux/arch/riscv/boot/Image -j16 
