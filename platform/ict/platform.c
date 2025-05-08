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
#include <sbi/sbi_trap.h>
#include <sbi_utils/serial/xlnx_uartlite.h>

/*
 * Include these files as needed.
 * See objects.mk PLATFORM_xxx configuration parameters.
 */
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/timer/aclint_mtimer.h>

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

#ifndef SERVE_HART_COUNT
#define SERVE_HART_COUNT	1
#endif

#define SERVE_PLIC_SIZE		(0x200000 + \
					 (SERVE_HART_COUNT * 0x1000))
#define SERVE_ACLINT_MTIMER_FREQ	10000000
#define SERVE_ACLINT_MSWI_ADDR	(SERVE_CLINT_ADDR + \
					 CLINT_MSWI_OFFSET)
#define SERVE_ACLINT_MTIMER_ADDR	(SERVE_CLINT_ADDR + \
					 CLINT_MTIMER_OFFSET)

static struct plic_data plic = {
	.addr = SERVE_PLIC_ADDR,
	.size = SERVE_PLIC_SIZE,
	.num_src = SERVE_PLIC_NUM_SOURCES,
	.context_map = {
		[0] = { 0, 1 },
		[1] = { 2, 3 },
		[2] = { 4, 5 },
		[3] = { 6, 7 },
	},
};

static struct aclint_mswi_data mswi = {
	.addr = SERVE_ACLINT_MSWI_ADDR,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = SERVE_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = SERVE_ACLINT_MTIMER_FREQ,
	.mtime_addr = SERVE_ACLINT_MTIMER_ADDR +
		      ACLINT_DEFAULT_MTIME_OFFSET,
	.mtime_size = ACLINT_DEFAULT_MTIME_SIZE,
	.mtimecmp_addr = SERVE_ACLINT_MTIMER_ADDR +
			 ACLINT_DEFAULT_MTIMECMP_OFFSET,
	.mtimecmp_size = ACLINT_DEFAULT_MTIMECMP_SIZE,
	.first_hartid = 0,
	.hart_count = SERVE_HART_COUNT,
	.has_64bit_mmio = true,
};

/*
 * Platform early initialization.
 */
static int platform_early_init(bool cold_boot)
{
	if (!cold_boot)
		return 0;

	/* Example if the generic UART8250 driver is used */
	return xlnx_uartlite_init(SERVE_UART0_ADDR);
}

/*
 * Platform final initialization.
 */
static int platform_final_init(bool cold_boot)
{
	return 0;
}

/*
 * Initialize the platform interrupt controller during cold boot.
 */
static int platform_irqchip_init(void)
{
	/* Example if the generic PLIC driver is used */
	return plic_cold_irqchip_init(&plic);
}

/*
 * Initialize IPI during cold boot.
 */
static int platform_ipi_init(void)
{
	/* Example if the generic ACLINT driver is used */
	return aclint_mswi_cold_init(&mswi);
}

/*
 * Initialize platform timer during cold boot.
 */
static int platform_timer_init(void)
{
	/* Example if the generic ACLINT driver is used */
	return aclint_mtimer_cold_init(&mtimer, NULL);
}

/*
 * Platform descriptor.
 */
const struct sbi_platform_operations platform_ops = {
	.early_init		= platform_early_init,
	.final_init		= platform_final_init,
	.irqchip_init		= platform_irqchip_init,
	.ipi_init		= platform_ipi_init,
	.timer_init		= platform_timer_init
};
const struct sbi_platform platform = {
	.opensbi_version	= OPENSBI_VERSION,
	.platform_version	= SBI_PLATFORM_VERSION(0x0, 0x00),
	.name			= "ICT SERVE",
	.features		= SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count		= SERVE_HART_COUNT,
	.hart_stack_size	= SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.heap_size		= SBI_PLATFORM_DEFAULT_HEAP_SIZE(1),
	.platform_ops_addr	= (unsigned long)&platform_ops
};
