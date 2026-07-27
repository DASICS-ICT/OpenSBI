/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_dasics.h>
#include <sbi/sbi_error.h>

#define DASICS_CSR_SFETCHCTL		0x9e0
#define DASICS_CSR_UMAIN_CFG		0x9e1
#define DASICS_CSR_UMAIN_BOUND_LO	0x9e2
#define DASICS_CSR_UMAIN_BOUND_HI	0x9e3
#define DASICS_CSR_SMAIN_CFG		0xbc0
#define DASICS_CSR_SMAIN_BOUND_LO	0xbc2
#define DASICS_CSR_SMAIN_BOUND_HI	0xbc3

#define DASICS_STATEEN_MASK		((1UL << 0) | (1UL << 63))
#define DASICS_MASK_OPS_VERSION		1UL
#define DASICS_MACHINE_MASK_TOTAL	64UL

#define DASICS_ALL_FDI_CSRS(M)					\
	M(0x880, FDILibCfg)					\
	M(0x890, FDILibBoundLo0)				\
	M(0x891, FDILibBoundHi0)				\
	M(0x892, FDILibBoundLo1)				\
	M(0x893, FDILibBoundHi1)				\
	M(0x894, FDILibBoundLo2)				\
	M(0x895, FDILibBoundHi2)				\
	M(0x896, FDILibBoundLo3)				\
	M(0x897, FDILibBoundHi3)				\
	M(0x898, FDILibBoundLo4)				\
	M(0x899, FDILibBoundHi4)				\
	M(0x89a, FDILibBoundLo5)				\
	M(0x89b, FDILibBoundHi5)				\
	M(0x89c, FDILibBoundLo6)				\
	M(0x89d, FDILibBoundHi6)				\
	M(0x89e, FDILibBoundLo7)				\
	M(0x89f, FDILibBoundHi7)				\
	M(0x8a0, FDILibBoundLo8)				\
	M(0x8a1, FDILibBoundHi8)				\
	M(0x8a2, FDILibBoundLo9)				\
	M(0x8a3, FDILibBoundHi9)				\
	M(0x8a4, FDILibBoundLo10)				\
	M(0x8a5, FDILibBoundHi10)				\
	M(0x8a6, FDILibBoundLo11)				\
	M(0x8a7, FDILibBoundHi11)				\
	M(0x8a8, FDILibBoundLo12)				\
	M(0x8a9, FDILibBoundHi12)				\
	M(0x8aa, FDILibBoundLo13)				\
	M(0x8ab, FDILibBoundHi13)				\
	M(0x8ac, FDILibBoundLo14)				\
	M(0x8ad, FDILibBoundHi14)				\
	M(0x8ae, FDILibBoundLo15)				\
	M(0x8af, FDILibBoundHi15)				\
	M(0x8b0, FDIMainCall)					\
	M(0x8b1, FDIReturnPC)					\
	M(0x8b2, FDIActiveReturn)				\
	M(0x8b3, FDIFReason)					\
	M(0x8c0, FDIJumpBoundLo0)				\
	M(0x8c1, FDIJumpBoundHi0)				\
	M(0x8c2, FDIJumpBoundLo1)				\
	M(0x8c3, FDIJumpBoundHi1)				\
	M(0x8c4, FDIJumpBoundLo2)				\
	M(0x8c5, FDIJumpBoundHi2)				\
	M(0x8c6, FDIJumpBoundLo3)				\
	M(0x8c7, FDIJumpBoundHi3)				\
	M(0x8c8, FDIJumpCfg)					\
	M(0x9e1, FDIUMainCfg)					\
	M(0x9e2, FDIUMainBoundLo)				\
	M(0x9e3, FDIUMainBoundHi)				\
	M(0xbc0, FDISMainCfg)					\
	M(0xbc2, FDISMainBoundLo)				\
	M(0xbc3, FDISMainBoundHi)

struct dasics_csr_case {
	const char *id;
	const char *name;
	unsigned long addr;
	unsigned long write;
	unsigned long expect;
};

static unsigned long dasics_csr_read(unsigned long addr)
{
	switch (addr) {
	case DASICS_CSR_SFETCHCTL:
		return csr_read(0x9e0);
	case DASICS_CSR_UMAIN_CFG:
		return csr_read(0x9e1);
	case DASICS_CSR_UMAIN_BOUND_LO:
		return csr_read(0x9e2);
	case DASICS_CSR_UMAIN_BOUND_HI:
		return csr_read(0x9e3);
	case DASICS_CSR_SMAIN_CFG:
		return csr_read(0xbc0);
	case DASICS_CSR_SMAIN_BOUND_LO:
		return csr_read(0xbc2);
	case DASICS_CSR_SMAIN_BOUND_HI:
		return csr_read(0xbc3);
	default:
		return 0;
	}
}

static void dasics_csr_write(unsigned long addr, unsigned long value)
{
	switch (addr) {
	case DASICS_CSR_SFETCHCTL:
		csr_write(0x9e0, value);
		break;
	case DASICS_CSR_UMAIN_CFG:
		csr_write(0x9e1, value);
		break;
	case DASICS_CSR_UMAIN_BOUND_LO:
		csr_write(0x9e2, value);
		break;
	case DASICS_CSR_UMAIN_BOUND_HI:
		csr_write(0x9e3, value);
		break;
	case DASICS_CSR_SMAIN_CFG:
		csr_write(0xbc0, value);
		break;
	case DASICS_CSR_SMAIN_BOUND_LO:
		csr_write(0xbc2, value);
		break;
	case DASICS_CSR_SMAIN_BOUND_HI:
		csr_write(0xbc3, value);
		break;
	}
}

static unsigned long dasics_mask_record(const char *operation,
					const char *name,
					unsigned long addr,
					unsigned long mask,
					unsigned long operand,
					unsigned long old,
					unsigned long expected_old,
					unsigned long read,
					unsigned long expected_read,
					unsigned long *total)
{
	bool pass = old == expected_old && read == expected_read;

	(*total)++;
	if (!pass)
		sbi_printf("FDI_CSR_MASK_OPS case=%s privilege=machine "
			   "csr=%s addr=0x%lx mask=0x%lx operand=0x%lx "
			   "old=0x%lx expected_old=0x%lx read=0x%lx "
			   "expected_read=0x%lx result=FAIL\r\n",
			   operation, name, addr, mask, operand, old,
			   expected_old, read, expected_read);

	return pass ? 0 : 1;
}

static unsigned long dasics_shared_record(const char *operation,
					  const char *view,
					  unsigned long operand,
					  unsigned long old,
					  unsigned long expected_old,
					  unsigned long expected_smain,
					  unsigned long expected_umain,
					  unsigned long *total)
{
	unsigned long smain = csr_read(0xbc0);
	unsigned long umain = csr_read(0x9e1);
	bool pass = old == expected_old && smain == expected_smain &&
		    umain == expected_umain;

	(*total)++;
	if (!pass)
		sbi_printf("FDI_CSR_MASK_OPS case=%s privilege=machine "
			   "csr=%s operand=0x%lx old=0x%lx "
			   "expected_old=0x%lx smain=0x%lx "
			   "expected_smain=0x%lx umain=0x%lx "
			   "expected_umain=0x%lx result=FAIL\r\n",
			   operation, view, operand, old, expected_old, smain,
			   expected_smain, umain, expected_umain);

	return pass ? 0 : 1;
}

static unsigned long dasics_mask_ops(void)
{
	unsigned long total = 0;
	unsigned long failures = 0;

	sbi_printf("FDI_CSR_MASK_OPS_BEGIN version=%lu privilege=machine "
		   "expected_total=%lu\r\n",
		   DASICS_MASK_OPS_VERSION, DASICS_MACHINE_MASK_TOTAL);

#define DASICS_CHECK_RESET(addr, name) do {				\
		unsigned long value = csr_read(addr);			\
		total++;						\
		if (value) {						\
			failures++;					\
			sbi_printf("FDI_CSR_MASK_OPS case=RESET "	\
				   "privilege=machine csr=%s addr=0x%lx "\
				   "read=0x%lx expected_read=0x0 "	\
				   "result=FAIL\r\n", #name,		\
				   (unsigned long)(addr), value);	\
		}							\
	} while (0);
	DASICS_ALL_FDI_CSRS(DASICS_CHECK_RESET)
#undef DASICS_CHECK_RESET

#define DASICS_RUN_BOUND_OPS(addr, name, seed) do {			\
		unsigned long mask = ~7UL;				\
		unsigned long writable_bit = mask & (0UL - mask);	\
		unsigned long write_operand =				\
			(0xa500000000000007UL ^			\
			 ((unsigned long)(seed) << 8)) & ~writable_bit;	\
		unsigned long expected = write_operand & mask;		\
		unsigned long set_operand = (~expected & mask) | ~mask;\
		unsigned long clear_operand = writable_bit | ~mask;	\
		unsigned long old = csr_swap(addr, write_operand);	\
		failures += dasics_mask_record("CSRRW", #name, addr,	\
				mask, write_operand, old, 0, csr_read(addr),\
				expected, &total);				\
		old = csr_read_set(addr, set_operand);			\
		{							\
			unsigned long expected_old = expected;		\
			expected |= set_operand & mask;			\
			failures += dasics_mask_record("CSRRS", #name,	\
					addr, mask, set_operand, old,	\
					expected_old, csr_read(addr),	\
					expected, &total);		\
		}							\
		old = csr_read_clear(addr, clear_operand);		\
		{							\
			unsigned long expected_old = expected;		\
			expected &= ~(clear_operand & mask);		\
			failures += dasics_mask_record("CSRRC", #name,	\
					addr, mask, clear_operand, old,	\
					expected_old, csr_read(addr),	\
					expected, &total);		\
		}							\
	} while (0)

	DASICS_RUN_BOUND_OPS(0xbc2, FDISMainBoundLo, 46);
	DASICS_RUN_BOUND_OPS(0xbc3, FDISMainBoundHi, 47);
#undef DASICS_RUN_BOUND_OPS

	{
		unsigned long smain_write = ~0x3ffUL | 0x155UL;
		unsigned long smain_set = ~0x3ffUL | 0x2aaUL;
		unsigned long smain_clear = ~0x3ffUL | 0x252UL;
		unsigned long umain_write = ~0x3eUL | 0x12UL;
		unsigned long umain_set = ~0x3eUL | 0x2cUL;
		unsigned long umain_clear = ~0x3eUL | 0x0aUL;
		unsigned long old = csr_swap(0xbc0, smain_write);

		failures += dasics_shared_record("CSRRW", "FDISMainCfg",
				smain_write, old, 0, 0x155, 0x14, &total);
		old = csr_read_set(0xbc0, smain_set);
		failures += dasics_shared_record("CSRRS", "FDISMainCfg",
				smain_set, old, 0x155, 0x3ff, 0x3e, &total);
		old = csr_read_clear(0xbc0, smain_clear);
		failures += dasics_shared_record("CSRRC", "FDISMainCfg",
				smain_clear, old, 0x3ff, 0x1ad, 0x2c, &total);
		old = csr_swap(0x9e1, umain_write);
		failures += dasics_shared_record("CSRRW", "FDIUMainCfg",
				umain_write, old, 0x2c, 0x193, 0x12, &total);
		old = csr_read_set(0x9e1, umain_set);
		failures += dasics_shared_record("CSRRS", "FDIUMainCfg",
				umain_set, old, 0x12, 0x1bf, 0x3e, &total);
		old = csr_read_clear(0x9e1, umain_clear);
		failures += dasics_shared_record("CSRRC", "FDIUMainCfg",
				umain_clear, old, 0x3e, 0x1b5, 0x34, &total);
	}

	csr_write(0xbc0, 0);
	csr_write(0xbc2, 0);
	csr_write(0xbc3, 0);
	if (total != DASICS_MACHINE_MASK_TOTAL)
		failures++;

	sbi_printf("FDI_CSR_MASK_OPS_SIGNATURE version=%lu privilege=machine "
		   "total=%lu failed=%lu result=%s\r\n",
		   DASICS_MASK_OPS_VERSION, total, failures,
		   failures ? "FAIL" : "PASS");

	return failures;
}

static unsigned long dasics_check_reset(const char *name, unsigned long addr,
					unsigned long *total)
{
	unsigned long read = dasics_csr_read(addr);
	bool pass = read == 0;

	(*total)++;
	if (!pass)
		sbi_printf("[DASICS-CSR] case=CSR-SMOKE-001 csr=%s "
			   "addr=0x%lx write=0x0 read=0x%lx expect=0x0 "
			   "result=FAIL\r\n", name, addr, read);

	return pass ? 0 : 1;
}

static unsigned long dasics_check_csr(const struct dasics_csr_case *test,
				      unsigned long *total)
{
	unsigned long read;
	bool pass;

	dasics_csr_write(test->addr, test->write);
	read = dasics_csr_read(test->addr);
	pass = read == test->expect;
	(*total)++;
	if (!pass)
		sbi_printf("[DASICS-CSR] case=%s csr=%s addr=0x%lx "
			   "write=0x%lx read=0x%lx expect=0x%lx "
			   "result=FAIL\r\n", test->id, test->name,
			   test->addr, test->write, read, test->expect);

	return pass ? 0 : 1;
}

static unsigned long dasics_check_shared(const char *id,
					 unsigned long smain_expect,
					 unsigned long umain_expect,
					 unsigned long *total)
{
	unsigned long smain = csr_read(0xbc0);
	unsigned long umain = csr_read(0x9e1);
	bool pass = smain == smain_expect && umain == umain_expect;

	(*total)++;
	if (!pass)
		sbi_printf("[DASICS-CSR] case=%s csr=DasicsMainCfg shared "
			   "smain=0x%lx umain=0x%lx expect_smain=0x%lx "
			   "expect_umain=0x%lx result=FAIL\r\n",
			   id, smain, umain, smain_expect, umain_expect);

	return pass ? 0 : 1;
}

static void dasics_clear_csrs(void)
{
	csr_write(0x9e0, 0);
	csr_write(0xbc0, 0);
	csr_write(0x9e2, 0);
	csr_write(0x9e3, 0);
	csr_write(0xbc2, 0);
	csr_write(0xbc3, 0);
}

static unsigned long dasics_smoke(void)
{
	static const struct dasics_csr_case direct_cases[] = {
		{ "CSR-SMOKE-014", "DasicsSMainBoundLo", 0xbc2,
		  0x5555555566666667UL, 0x5555555566666660UL },
		{ "CSR-SMOKE-014", "DasicsSMainBoundHi", 0xbc3,
		  0x777777778888888fUL, 0x7777777788888888UL },
	};
	unsigned long stateen;
	unsigned long total = 0;
	unsigned long failures = 0;
	unsigned long i;

	sbi_printf("[DASICS-CSR] machine smoke begin\r\n");
	stateen = csr_read(0x30c);
	total++;
	if ((stateen & DASICS_STATEEN_MASK) != DASICS_STATEEN_MASK)
		failures++;

	failures += dasics_check_reset("DasicsSMainCfg", 0xbc0, &total);
	failures += dasics_check_reset("DasicsSMainBoundLo", 0xbc2, &total);
	failures += dasics_check_reset("DasicsSMainBoundHi", 0xbc3, &total);

	csr_write(0xbc0, ~0UL);
	failures += dasics_check_shared("CSR-SMOKE-002", 0x3ff, 0x3e,
					&total);
	csr_write(0x9e1, 0);
	failures += dasics_check_shared("CSR-SMOKE-003", 0x3c1, 0, &total);
	csr_write(0x9e1, 0x3e);
	failures += dasics_check_shared("CSR-SMOKE-004", 0x3ff, 0x3e,
					&total);

	for (i = 0; i < sizeof(direct_cases) / sizeof(direct_cases[0]); i++)
		failures += dasics_check_csr(&direct_cases[i], &total);

	dasics_clear_csrs();
	sbi_printf("[DASICS-CSR] machine summary total=%lu failed=%lu "
		   "result=%s\r\n", total, failures,
		   failures ? "FAIL" : "PASS");

	return failures;
}

int sbi_dasics_test(void)
{
	unsigned long failures;

	failures = dasics_mask_ops();
	failures += dasics_smoke();
	return failures ? SBI_EFAIL : 0;
}
