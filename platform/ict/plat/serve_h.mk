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
FW_PAYLOAD_FDT_ADDR=0x88000000

# platform-specific macro define for platform.c
platform-genflags-y += -DSERVE_UART0_ADDR=0x30000000 \
	-DUART_REG_RX_FIFO=0x00 \
	-DUART_REG_TX_FIFO=0x04 \
	-DUART_REG_CH_STAT=0x08 \
	-DUART_TXFIFO_FULL_BIT=3 \
	-DUART_RXFIFO_VALID_DATA_BIT=0 \
	-DSERVE_ECALL_EXT=0

