/* SPDX-License-Identifier: BSD-2-Clause */
/* Port of the reference kernel's S-main setup extension. */
#include <sbi/riscv_asm.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_trap.h>

static int sbi_ecall_dasics_handler(unsigned long extid, unsigned long funcid,
				  struct sbi_trap_regs *regs,
				  struct sbi_ecall_return *out)
{
	if (extid != 0xda51c5 || funcid != 1)
		return SBI_EINVAL;

	csr_write(0xbc3, regs->a1);
	csr_write(0xbc2, regs->a0);
	csr_write(0xbc0, regs->a2);
	return 0;
}

struct sbi_ecall_extension ecall_dasics;

static int sbi_ecall_dasics_register_extensions(void)
{
	return sbi_ecall_register_extension(&ecall_dasics);
}

struct sbi_ecall_extension ecall_dasics = {
	.extid_start = 0xda51c5,
	.extid_end = 0xda51c5,
	.register_extensions = sbi_ecall_dasics_register_extensions,
	.handle = sbi_ecall_dasics_handler,
};
