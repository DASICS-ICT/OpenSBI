rm ./dts/platform.dtsi
ln -s noop.dtsi ./dts/platform.dtsi

make PLATFORM=generic CROSS_COMPILE=riscv64-unknown-linux-gnu- FW_PAYLOAD_PATH=../riscv-linux/arch/riscv/boot/Image -j16