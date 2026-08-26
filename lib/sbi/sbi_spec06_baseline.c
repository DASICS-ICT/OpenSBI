// SPDX-License-Identifier: BSD-2-Clause

#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_error.h>

#define SPEC06_CSR_FDIU_MAIN_CFG 0x9e1
#define SPEC06_CSR_FDIS_MAIN_CFG 0xbc0

int sbi_spec06_baseline_disable(void)
{
	unsigned long smain;
	unsigned long umain;

	/* The S view owns both enable bits; zero disables S and U enforcement. */
	csr_write(SPEC06_CSR_FDIS_MAIN_CFG, 0);
	smain = csr_read(SPEC06_CSR_FDIS_MAIN_CFG);
	umain = csr_read(SPEC06_CSR_FDIU_MAIN_CFG);
	sbi_printf("SPEC06_BASELINE_FDI_SWITCH version=1 stage=handoff "
		   "smain=0x%016lx umain=0x%016lx result=%s\r\n",
		   smain, umain, (smain || umain) ? "FAIL" : "PASS");

	return (smain || umain) ? SBI_EFAIL : 0;
}
