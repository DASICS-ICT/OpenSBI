/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 ICT CAS.
 *
 * Authors:
 *   Siyuan Zeng <zengsiyuan17@gmail.com>
 */

#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_csr_detect.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>

static int sbi_ecall_zicfilp_handler(unsigned long extid, unsigned long funcid,
				     const struct sbi_trap_regs *regs,
				     unsigned long *out_val,
				     struct sbi_trap_info *out_trap)
{
	int ret = 0;
	unsigned long senvcfg_val;
	struct sbi_trap_info trap = {0};

	switch (funcid) {
	case SBI_EXT_ZICFILP_SET_SENVCFG_LPE:
		/*
		 * Set senvcfg.LPE bit based on parameter in regs->a0
		 * a0 = 0: disable Zicfilp in S-mode
		 * a0 = 1: enable Zicfilp in S-mode
		 * Returns: SBI_SUCCESS on success
		 *          SBI_ERR_INVALID_PARAM if parameter invalid
		 *          SBI_ERR_NOT_SUPPORTED if senvcfg not available
		 */
		if (regs->a0 > 1) {
			ret = SBI_ERR_INVALID_PARAM;
			break;
		}

		/* Try to read senvcfg to check if it's supported */
		senvcfg_val = csr_read_allowed(CSR_SENVCFG, (unsigned long)&trap);
		if (trap.cause) {
			ret = SBI_ERR_NOT_SUPPORTED;
			break;
		}

		/* Set or clear the LPE bit */
		if (regs->a0 == 1) {
			senvcfg_val |= ENVCFG_LPE;
		} else {
			senvcfg_val &= ~ENVCFG_LPE;
		}

		/* Write back to senvcfg */
		trap.cause = 0;
		csr_write_allowed(CSR_SENVCFG, (unsigned long)&trap, senvcfg_val);
		if (trap.cause) {
			ret = SBI_ERR_FAILED;
			break;
		}

		ret = SBI_SUCCESS;
		*out_val = 0;
		break;

	case SBI_EXT_ZICFILP_GET_SENVCFG_LPE:
		/*
		 * Get senvcfg.LPE bit status
		 * Returns: 0 if disabled, 1 if enabled
		 *          SBI_ERR_NOT_SUPPORTED if senvcfg not available
		 */

		/* Try to read senvcfg */
		senvcfg_val = csr_read_allowed(CSR_SENVCFG, (unsigned long)&trap);
		if (trap.cause) {
			ret = SBI_ERR_NOT_SUPPORTED;
			break;
		}

		/* Extract LPE bit and return in out_val */
		*out_val = (senvcfg_val & ENVCFG_LPE) ? 1 : 0;
		ret = SBI_SUCCESS;
		break;

	default:
		ret = SBI_ENOTSUPP;
		break;
	}

	return ret;
}

struct sbi_ecall_extension ecall_zicfilp = {
	.extid_start = SBI_EXT_ZICFILP,
	.extid_end = SBI_EXT_ZICFILP,
	.handle = sbi_ecall_zicfilp_handler,
};