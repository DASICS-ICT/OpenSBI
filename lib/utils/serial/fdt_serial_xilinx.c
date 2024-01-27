/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021 Guokai Chen <chenguokai17@mails.ucas.ac.cn>
 *
 */

#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/serial/fdt_serial.h>
#include <sbi_utils/serial/xilinx-uart.h>

// ======== UARTLITE ========

static int serial_xilinx_uartlite_init(void *fdt, int nodeoff,
				const struct fdt_match *match)
{
		
	int rc;
	struct platform_uart_data uart;
	// no need to reinvent a wheel
	rc = fdt_parse_shakti_uart_node(fdt, nodeoff, &uart);
	if (rc)
		return rc;
	return xilinx_uartlite_init(uart.addr, uart.freq, uart.baud);
}

static const struct fdt_match serial_xilinx_uartlite_match[] = {
	{ .compatible = "xilinx,uartlite" },
	{ },
};

struct fdt_serial fdt_serial_xilinx_uartlite = {
	.match_table = serial_xilinx_uartlite_match,
	.init = serial_xilinx_uartlite_init,
	.getc = xilinx_uartlite_getc,
	.putc = xilinx_uartlite_putc
};


// ======== XUARTPS ========

static int serial_xilinx_xuartps_init(void *fdt, int nodeoff,
				const struct fdt_match *match)
{
	int rc;
	struct platform_uart_data uart;
	uart.addr = 0;
	// no need to reinvent a wheel
	rc = fdt_parse_shakti_uart_node(fdt, nodeoff, &uart);
	if (rc)
		return rc;
	return xilinx_xuartps_init(uart.addr, uart.freq, uart.baud);
}

static const struct fdt_match serial_xilinx_xuartps_match[] = {
	{ .compatible = "xlnx,xuartps" },
	{ },
};

struct fdt_serial fdt_serial_xilinx_xuartps = {
	.match_table = serial_xilinx_xuartps_match,
	.init = serial_xilinx_xuartps_init,
	.getc = xilinx_xuartps_getc,
	.putc = xilinx_xuartps_putc
};
