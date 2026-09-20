/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef __FDT_SERIAL_DDR_H__
#define __FDT_SERIAL_DDR_H__

/* SBI_ENODEV means stdout-path did not select this backend. */
int fdt_serial_ddr_init(void *fdt);

#endif
