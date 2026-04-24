#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2020 Institute of Computing Technology, Chinese Academy of Sciences.
#
# Authors:
#   Yisong Chang <changyisong@ict.ac.cn>
#

# Blobs to build
FW_TEXT_START=0x80000000
FW_PAYLOAD_FDT_ADDR=0x90000000

# platform-specific macro define for platform.c
# CLINT/PLIC base addresses for Nanhu s2c_vu19p real hardware.
# xs-gen SoC.scala pins CLINT@0x38000000 and PLIC@0x3c000000 (XSTop.dts
# agrees). platform/ict/platform.c defaults (0x2000000 / 0xc000000) are
# from earlier ICT boards and do not apply to s2c_vu19p; override here
# via the #ifndef hooks opened by upstream commit 8828764.
platform-genflags-y += -DSERVE_UART0_ADDR=0x30000000 \
	-DSERVE_CLINT_ADDR=0x38000000 \
	-DSERVE_PLIC_ADDR=0x3c000000 \
	-DUART_REG_RX_FIFO=0x00 \
	-DUART_REG_TX_FIFO=0x04 \
	-DUART_REG_CH_STAT=0x08 \
	-DUART_TXFIFO_FULL_BIT=3 \
	-DUART_RXFIFO_VALID_DATA_BIT=0 \
	-DSERVE_ECALL_EXT=0

