/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * RISC-V ZIMT: optional S-mode control via SBI (experimental).
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>

static int sbi_ecall_zimt_probe(unsigned long extid, unsigned long *out_val)
{
	struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

	(void)extid;
	*out_val = sbi_hart_has_feature(scratch, SBI_HART_HAS_ZIMT) ? 1 : 0;
	return 0;
}

static int sbi_ecall_zimt_handle(unsigned long extid, unsigned long funcid,
				 const struct sbi_trap_regs *regs,
				 unsigned long *out_val,
				 struct sbi_trap_info *out_trap)
{
	unsigned long val, mode;

	(void)extid;
	(void)out_trap;

	if (!sbi_hart_has_feature(sbi_scratch_thishart_ptr(), SBI_HART_HAS_ZIMT))
		return SBI_ENOTSUPP;

	switch (funcid) {
	case SBI_EXT_ZIMT_ENABLE:
		/* a0: MT_MODE nibble (0=off, 2=4-bit, 3=7/8-bit) */
		mode = regs->a0 & 0x3UL;
		val = csr_read(CSR_MENVCFG);
		val = (val & ~ENVCFG_MT_MODE) | (mode << 34);
		csr_write(CSR_MENVCFG, val);
		*out_val = 0;
		break;
	case SBI_EXT_ZIMT_DISABLE:
		csr_clear(CSR_MENVCFG, ENVCFG_MT_MODE);
		*out_val = 0;
		break;
	case SBI_EXT_ZIMT_GET_TAG_BITS:
		mode = (csr_read(CSR_MENVCFG) & ENVCFG_MT_MODE) >> 34;
		if (mode == 2)
			*out_val = 4;
		else if (mode == 3)
			*out_val = 8;
		else
			*out_val = 0;
		return 0;
	case SBI_EXT_ZIMT_SET_TAG_MASK:
		/* Reserved until STVAL_MASK CSR is fixed per platform */
		*out_val = 0;
		break;
	default:
		return SBI_ENOTSUPP;
	}

	return 0;
}

struct sbi_ecall_extension ecall_zimt = {
	.extid_start = SBI_EXT_ZIMT,
	.extid_end = SBI_EXT_ZIMT,
	.probe = sbi_ecall_zimt_probe,
	.handle = sbi_ecall_zimt_handle,
};
