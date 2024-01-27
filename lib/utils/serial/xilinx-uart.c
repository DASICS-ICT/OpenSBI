/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021 Guokai Chen <chenguokai17@mails.ucas.ac.cn>
 */

#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi_utils/serial/xilinx-uart.h>

#define REG_TX		0x04
#define REG_RX		0x00
#define REG_STATUS	0x08
#define REG_CONTROL	0x0C

#define UART_TX_FULL    (1<<0x3)
#define UART_RX_VALID   (1<<0x0)

static volatile void *uart_base;

// ======== UARTLITE ========

void xilinx_uartlite_putc(char ch)
{
	while((readb(uart_base + REG_STATUS) & UART_TX_FULL))
		;
	writeb(ch, uart_base + REG_TX);
}

int xilinx_uartlite_getc(void)
{
	u16 status = readb(uart_base + REG_STATUS);
	if (status & UART_RX_VALID)
		return readb(uart_base + REG_RX);
	return -1;
}

int xilinx_uartlite_init(unsigned long base, u32 in_freq, u32 baudrate)
{
	uart_base = (volatile void *)base;
	return 0;
}

// ======== XUARTPS ========


static volatile uint32_t* xuart = NULL; //(void *)0x40010000;

static inline void writel_ps(uint32_t val, volatile void *addr)
{
  __iowmb();
  __arch_putl(val, addr);
}
static inline uint32_t readl_ps(const volatile void *addr)
{
  uint32_t val;

  val = __arch_getl(addr);
  __iormb();
  return val;
}

int xilinx_xuartps_getc()
{
  struct uart_zynq *regs = (struct uart_zynq*)xuart;

  if (readl_ps(&regs->channel_sts) & ZYNQ_UART_SR_RXEMPTY)
    return -1;
  
  return readl_ps(&regs->tx_rx_fifo);
}

void xilinx_xuartps_putc(char ch)
{
  struct uart_zynq *regs = (struct uart_zynq*)xuart;
  while (readl_ps(&regs->channel_sts) & ZYNQ_UART_SR_TXFULL);
  writel_ps(ch, &regs->tx_rx_fifo);
}

int xilinx_xuartps_init(unsigned long base, u32 in_freq, u32 baudrate)
{
  xuart = (uint32_t*)base;
  struct uart_zynq *regs = (struct uart_zynq *)xuart;
	  /* RX/TX enabled & reset */
  writel_ps(ZYNQ_UART_CR_TX_EN | ZYNQ_UART_CR_RX_EN | ZYNQ_UART_CR_TXRST |
        ZYNQ_UART_CR_RXRST| ZYNQ_UART_CR_TORST, &regs->control);
  writel_ps(ZYNQ_UART_MR_PARITY_NONE, &regs->mode); /* 8 bit, no parity */
  return 0;
}
