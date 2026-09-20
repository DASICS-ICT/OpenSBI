/* SPDX-License-Identifier: BSD-2-Clause */
/* Nanhu DDR output console. The host reads only after stopping the CPU. */
#include <libfdt.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_string.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/serial/fdt_serial.h>
#include <sbi_utils/serial/fdt_serial_ddr.h>

#define DDR_COMPAT "bosc,nanhu-ddr-console"
#define DDR_MAGIC 0x555648534c4f4731ULL
#define CACHE_LINE 64UL

/* Little-endian RV64 format, one cache line; version 2 has no done marker. */
struct ddr_header {
	u64 magic, length, checksum, overflow, reserved, cycle, time, version;
};
static volatile struct ddr_header *header;
static volatile unsigned char *buffer;
static unsigned long capacity, used, checksum;
static spinlock_t ddr_lock = SPIN_LOCK_INITIALIZER;

static void flush_line(unsigned long address)
{
	/* Nanhu supports Zicbom and 64-byte cache blocks; older GAS needs encoding. */
	register unsigned long addr asm("a0") = address;
	asm volatile(".word 0x0025200f" : : "r"(addr) : "memory");
}

static unsigned long ddr_puts(const char *str, unsigned long len)
{
	unsigned long i, first;

	spin_lock(&ddr_lock);
	first = used & ~(CACHE_LINE - 1);
	for (i = 0; i < len && used < capacity; i++) {
		buffer[used++] = (unsigned char)str[i];
		checksum += (unsigned char)str[i];
	}
	if (i != len)
		header->overflow = 1;
	/* Publish the body before its metadata, including a tail without newline. */
	asm volatile("fence iorw, iorw" ::: "memory");
	for (i = first; i < used; i += CACHE_LINE)
		flush_line((unsigned long)buffer + i);
	asm volatile("fence iorw, iorw" ::: "memory");
	header->length = used;
	header->checksum = checksum;
	header->cycle = csr_read(CSR_CYCLE);
	header->time = csr_read(CSR_TIME);
	flush_line((unsigned long)header);
	asm volatile("fence iorw, iorw" ::: "memory");
	spin_unlock(&ddr_lock);
	/* A full log drops new bytes rather than blocking the workload. */
	return len;
}

static void ddr_putc(char ch)
{
	ddr_puts(&ch, 1);
}

static int ddr_getc(void) { return -1; }

static const struct sbi_console_device ddr_console = {
	.name = "ddr",
	.console_putc = ddr_putc,
	.console_puts = ddr_puts,
	.console_getc = ddr_getc,
};

static int stdout_node(void *fdt)
{
	int chosen = fdt_path_offset(fdt, "/chosen"), len;
	const char *path, *sep;

	if (chosen < 0)
		return chosen;
	path = fdt_getprop(fdt, chosen, "stdout-path", &len);
	if (!path || len < 2 || path[len - 1])
		return -1;
	sep = strchr(path, ':');
	return sep ? fdt_path_offset_namelen(fdt, path, sep - path) :
		     fdt_path_offset(fdt, path);
}

static int serial_ddr_init(void *fdt, int node,
			   const struct fdt_match *match)
{
	const fdt32_t *prop;
	int region, len, rc;
	u64 addr, size;

	/* Do not activate by fallback scanning: DDR must be explicitly selected. */
	if (node != stdout_node(fdt))
		return SBI_ENODEV;
#if __riscv_xlen != 64
	return SBI_ENOTSUPP;
#endif
	if (!fdt_node_is_enabled(fdt, node))
		return SBI_EINVAL;
	prop = fdt_getprop(fdt, node, "memory-region", &len);
	if (!prop || len != sizeof(*prop))
		return SBI_EINVAL;
	region = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop));
	if (region < 0 || !fdt_node_is_enabled(fdt, region) ||
	    fdt_parent_offset(fdt, region) != fdt_path_offset(fdt, "/reserved-memory") ||
	    !fdt_getprop(fdt, region, "no-map", NULL))
		return SBI_EINVAL;
	rc = fdt_get_node_addr_size(fdt, region, 0, &addr, &size);
	if (rc)
		return rc;
	if (!addr || size < 2 * CACHE_LINE || (addr | size) & (CACHE_LINE - 1) ||
	    addr > (unsigned long)-1 || size > (unsigned long)-1 - addr)
		return SBI_EINVAL;
	header = (void *)(unsigned long)addr;
	buffer = (void *)(unsigned long)(addr + sizeof(*header));
	capacity = size - sizeof(*header);
	used = checksum = 0;
	for (len = 0; len < sizeof(*header) / sizeof(u64); len++)
		((volatile u64 *)header)[len] = 0;
	header->magic = DDR_MAGIC;
	header->version = 2;
	asm volatile("fence iorw, iorw" ::: "memory");
	flush_line(addr);
	asm volatile("fence iorw, iorw" ::: "memory");
	sbi_console_set_device(&ddr_console);
	return 0;
}

int fdt_serial_ddr_init(void *fdt)
{
	int node = stdout_node(fdt);

	if (node < 0 || fdt_node_check_compatible(fdt, node, DDR_COMPAT))
		return SBI_ENODEV;
	return serial_ddr_init(fdt, node, NULL);
}

static const struct fdt_match serial_ddr_match[] = {
	{ .compatible = DDR_COMPAT },
	{ },
};

struct fdt_serial fdt_serial_ddr = {
	.match_table = serial_ddr_match,
	.init = serial_ddr_init,
};
