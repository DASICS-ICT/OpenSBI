/* SPDX-License-Identifier: BSD-2-Clause */
/* Port of the reference kernel's S-main setup extension. */
#include <sbi/riscv_asm.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>

static int sbi_ecall_dasics_handler(unsigned long extid, unsigned long funcid,
				  struct sbi_trap_regs *regs,
				  struct sbi_ecall_return *out)
{
	if (extid != 0xda51c5)
		return SBI_EINVAL;

#ifdef CONFIG_DASICS_VALIDATION
	/* Read-only observation for the restricted Linux software-test interface. */
	if (funcid == 2) {
		if (regs->a0 || regs->a1 || regs->a2 || regs->a3 || regs->a4 || regs->a5)
			return SBI_EINVAL;
		if (sbi_hart_priv_version(sbi_scratch_thishart_ptr()) < SBI_HART_PRIV_VER_1_12)
			return SBI_ENOTSUPP;
		out->value = csr_read(CSR_MENVCFG) & 0xf0UL;
		return 0;
	}
#endif
	if (funcid != 1)
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
