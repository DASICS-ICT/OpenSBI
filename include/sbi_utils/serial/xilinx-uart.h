/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 Vijai Kumar K <vijai@behindbytes.com>
 */

#ifndef __SERIAL_XILINX_UART_H__
#define __SERIAL_XILINX_UART_H__

#include <sbi/sbi_types.h>

#define XUARTPS_FIFO_OFFSET 0x0000000c
#define XUARTPS_SR_OFFSET 0x0000000b
#define SR_TX_FULL  0x10
#define SR_RX_EMPTY 0x02

#define BIT(nr)         (1 << (nr))

#define RISCV_FENCE(p, s) \
        __asm__ __volatile__ ("fence " #p "," #s : : : "memory")

/* These barriers need to enforce ordering on both devices or memory */
#define mb()            RISCV_FENCE(iorw,iorw)
#define rmb()           RISCV_FENCE(ir,ir)
#define wmb()           RISCV_FENCE(ow,ow)

#define dmb()           mb()
#define __iormb()       rmb()
#define __iowmb()       wmb()

#define __arch_getl(a)        (*(unsigned int *)(a))
#define __arch_putl(v, a)     (*(unsigned int *)(a) = (v))

#define ZYNQ_UART_SR_TXACTIVE   BIT(11) /* TX active */
#define ZYNQ_UART_SR_TXFULL     BIT(4)  /* TX FIFO full */
#define ZYNQ_UART_SR_RXEMPTY    BIT(1)  /* RX FIFO empty */

#define ZYNQ_UART_CR_TORST     BIT(6) /* RX logic reset */
#define ZYNQ_UART_CR_TX_EN     BIT(4) /* TX enabled */
#define ZYNQ_UART_CR_RX_EN     BIT(2) /* RX enabled */
#define ZYNQ_UART_CR_TXRST     BIT(1) /* TX logic reset */
#define ZYNQ_UART_CR_RXRST     BIT(0) /* RX logic reset */

#define ZYNQ_UART_MR_PARITY_NONE  0x00000020    /* No parity mode */
#define XUARTPS_MR_CHMODE_R_LOOP       0x00000300U /**< Remote loopback mode */
#define XUARTPS_MR_CHMODE_L_LOOP       0x00000200U /**< Local loopback mode */
#define XUARTPS_MR_CHMODE_ECHO         0x00000100U /**< Auto echo mode */
#define XUARTPS_MR_CHMODE_NORM         0x00000000U /**< Normal mode */

struct uart_zynq {
       uint32_t control; /* 0x0 - Control Register [8:0] */
       uint32_t mode; /* 0x4 - Mode Register [10:0] */
       uint32_t reserved1[4];
       uint32_t baud_rate_gen; /* 0x18 - Baud Rate Generator [15:0] */
       uint32_t reserved2[4];
       uint32_t channel_sts; /* 0x2c - Channel Status [11:0] */
       uint32_t tx_rx_fifo; /* 0x30 - FIFO [15:0] or [7:0] */
       uint32_t baud_rate_divider; /* 0x34 - Baud Rate Divider [7:0] */
};

void xilinx_uartlite_putc(char ch);

int xilinx_uartlite_getc(void);

int xilinx_uartlite_init(unsigned long base, u32 in_freq, u32 baudrate);

void xilinx_xuartps_putc(char ch);

int xilinx_xuartps_getc(void);

int xilinx_xuartps_init(unsigned long base, u32 in_freq, u32 baudrate);

#endif