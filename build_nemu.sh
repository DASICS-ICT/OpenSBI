rm ./dts/platform.dtsi
ln -s noop.dtsi ./dts/platform.dtsi

dtc -O dtb -o ./build/system.dtb ./dts/system.dts

make PLATFORM=generic CROSS_COMPILE=riscv64-unknown-linux-gnu- FW_FDT_PATH=system.dtb FW_PAYLOAD_PATH=../riscv-linux/arch/riscv/boot/Image -j16 