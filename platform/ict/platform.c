/*
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2019 Institute of Computing Technology, Chinese Academy of Sciences.
 *
 * Authors:
 *   Yisong Chang <changyisong@ict.ac.cn>
 */

#include <libfdt.h>
#include <fdt.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_platform.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_io.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/sys/clint.h>
#include <sbi/sbi_trap.h>

/* clang-format off */

#ifndef SERVE_CLINT_ADDR
#define SERVE_CLINT_ADDR		0x2000000
#endif

#ifndef SERVE_PLIC_ADDR
#define SERVE_PLIC_ADDR			0xc000000
#endif

#ifndef SERVE_PLIC_NUM_SOURCES
#define SERVE_PLIC_NUM_SOURCES	16
#endif

/* UART base address was defined in plat/serve_X.mk */
#define UART_TXFIFO_FULL	(1 << UART_TXFIFO_FULL_BIT)
#ifdef UART_RXFIFO_EMPTY_BIT
#define UART_RXFIFO_EMPTY	(1 << UART_RXFIFO_EMPTY_BIT)
#endif
#ifdef UART_RXFIFO_VALID_DATA_BIT
#define UART_RXFIFO_VALID_DATA	(1 << UART_RXFIFO_VALID_DATA_BIT)
#endif
#define UART_RXFIFO_DATA	0x000000ff

#define SERVE_HART_STACK_SIZE		8192

#define SERVE_ENABLED_HART_MASK		((1 << SERVE_HART_COUNT) - 1)	

#define SERVE_HARITD_DISABLED		~(SERVE_ENABLED_HART_MASK)

#define SERVE_EXT_PM        0x09000000

#define PM_SIP_SVC          0xC2000000

#ifndef RV_ARM_IPC_BASE
#define RV_ARM_IPC_BASE   0xf0000000
#endif
#define IPC_REQ    ((volatile void *)(RV_ARM_IPC_BASE + 0x0))
#define IPC_API    ((volatile void *)(RV_ARM_IPC_BASE + 0x4))
#define IPC_ARG_0  ((volatile void *)(RV_ARM_IPC_BASE + 0x10))
#define IPC_ARG_1  ((volatile void *)(RV_ARM_IPC_BASE + 0x14))
#define IPC_ARG_2  ((volatile void *)(RV_ARM_IPC_BASE + 0x18))
#define IPC_ARG_3  ((volatile void *)(RV_ARM_IPC_BASE + 0x1c))
#define IPC_RET_0  ((volatile void *)(RV_ARM_IPC_BASE + 0x20))
#define IPC_RET_1  ((volatile void *)(RV_ARM_IPC_BASE + 0x24))
#define IPC_RET_2  ((volatile void *)(RV_ARM_IPC_BASE + 0x28))
#define IPC_RET_3  ((volatile void *)(RV_ARM_IPC_BASE + 0x2c))

static volatile void *uart_base;

static spinlock_t pm_secure_lock;

static struct plic_data plic = {
	.addr = SERVE_PLIC_ADDR,
	.num_src = SERVE_PLIC_NUM_SOURCES,
};

static struct clint_data clint = {
	.addr = SERVE_CLINT_ADDR,
	.first_hartid = 0,
	.hart_count = SERVE_HART_COUNT,
	.has_64bit_mmio = TRUE,
};

/* clang-format on */

static inline void set_uart_base()
{
	uart_base = (volatile void *)SERVE_UART0_ADDR;
}

static inline u32 get_reg(u32 offset)
{
	return readl(uart_base + offset);
}

static inline void set_reg(u32 num, u32 offset)
{
	writel(num, uart_base + offset);
}

void serve_uart_putc(char ch)
{
	while (get_reg(UART_REG_CH_STAT) & UART_TXFIFO_FULL)
		;

	set_reg(ch, UART_REG_TX_FIFO);
}

int serve_uart_getc(void)
{
	u32 ret = get_reg(UART_REG_CH_STAT);
#ifdef UART_RXFIFO_EMPTY
	if (!(ret & UART_RXFIFO_EMPTY))
#else
	if (ret & UART_RXFIFO_VALID_DATA)
#endif
	{
		ret = get_reg(UART_REG_RX_FIFO);
		return ret & UART_RXFIFO_DATA;
	}
	return -1;
}

static int serve_early_init(bool cold_boot)
{
	if (!cold_boot)
		return 0;

	set_uart_base();
	SPIN_LOCK_INIT(&pm_secure_lock);

	return 0;
}

static int serve_final_init(bool cold_boot)
{
	return 0;
}

static int serve_irqchip_init(bool cold_boot)
{
	int rc;
	u32 hartid = current_hartid();

	if (cold_boot) {
		rc = plic_cold_irqchip_init(&plic);
		if (rc)
			return rc;
	}

	return plic_warm_irqchip_init(&plic, (hartid) ? (2 * hartid - 1) : 0,
					  (hartid) ? (2 * hartid) : -1);
}

static int serve_ipi_init(bool cold_boot)
{
	int rc;

	if (cold_boot) {
		rc = clint_cold_ipi_init(&clint);
		if (rc)
			return rc;
	}

	return clint_warm_ipi_init();
}

static int serve_timer_init(bool cold_boot)
{
	int rc;

	if (cold_boot) {
		rc = clint_cold_timer_init(&clint, NULL);
		if (rc)
			return rc;
	}

	return clint_warm_timer_init();
}

static int serve_vendor_ext_check(long extid) {
#if SERVE_ECALL_EXT
	switch (extid) {
		case SERVE_EXT_PM:
			return 1;
		default:
			return 0;
	}
#else
	return 0;
#endif
}

#if SERVE_ECALL_EXT
static int pm_ecall_handler(long funcid,
	const struct sbi_trap_regs *regs, unsigned long *out_value)
{
	spin_lock(&pm_secure_lock);

	writel(PM_SIP_SVC | funcid, IPC_API);
	writel(regs->a0, IPC_ARG_0);
	writel(regs->a1, IPC_ARG_1);
	writel(regs->a2, IPC_ARG_2);
	writel(regs->a3, IPC_ARG_3);

	writel(1, IPC_REQ);
	while (readl(IPC_REQ));

	out_value[0] = readl(IPC_RET_0) | (unsigned long)readl(IPC_RET_1) << 32;
	out_value[1] = readl(IPC_RET_2) | (unsigned long)readl(IPC_RET_3) << 32;

	spin_unlock(&pm_secure_lock);

	return 0;
}
#endif

static int serve_vendor_ext_provider(long extid, long funcid,
	const struct sbi_trap_regs *regs, unsigned long *out_value,
	struct sbi_trap_info *out_trap)
{
#if SERVE_ECALL_EXT
	switch (extid) {
		case SERVE_EXT_PM:
			return pm_ecall_handler(funcid, regs, out_value);
		default:
			return SBI_ENOTSUPP;
	}
#else
	return SBI_ENOTSUPP;
#endif
}

const struct sbi_platform_operations platform_ops = {
	.early_init		= serve_early_init,
	.final_init		= serve_final_init,
	.console_putc		= serve_uart_putc,
	.console_getc		= serve_uart_getc,
	.irqchip_init		= serve_irqchip_init,
	.ipi_send		= clint_ipi_send,
	.ipi_clear		= clint_ipi_clear,
	.ipi_init		= serve_ipi_init,
	.timer_value		= clint_timer_value,
	.timer_event_stop	= clint_timer_event_stop,
	.timer_event_start	= clint_timer_event_start,
	.timer_init		= serve_timer_init,
	.vendor_ext_check	= serve_vendor_ext_check,
	.vendor_ext_provider    = serve_vendor_ext_provider,
};

const struct sbi_platform platform = {
	.opensbi_version	= OPENSBI_VERSION,
	.platform_version	= SBI_PLATFORM_VERSION(0x0, 0x01),
	.name			= "ICT SERVE",
	.features		= SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count		= SERVE_HART_COUNT,
	.hart_stack_size	= SERVE_HART_STACK_SIZE,
	.platform_ops_addr	= (unsigned long)&platform_ops
};
