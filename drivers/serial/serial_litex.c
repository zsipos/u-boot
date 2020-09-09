// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <clk.h>
#include <debug_uart.h>
#include <dm.h>
#include <errno.h>
#include <fdtdec.h>
#include <log.h>
#include <watchdog.h>
#include <asm/io.h>
#include <linux/compiler.h>
#include <serial.h>
#include <linux/err.h>

#include "litex_csroffsets.h"

DECLARE_GLOBAL_DATA_PTR;

struct litex_uart_platdata {
	unsigned int *regs;
};

static inline unsigned char csr_readb(void *addr, int offset)
{
	addr += offset;
	return *(volatile unsigned int*)addr;
}

static inline void csr_writeb(unsigned char val, void *addr, int offset)
{
	addr += offset;
	*(volatile unsigned int*)addr = val;
}

void litexdbg(int x)
{
	csr_writeb(x, 0x1200e000, LITEX_GPIO0_OUT_REG);
}

static int litex_serial_setbrg(struct udevice *dev, int baudrate)
{
	// uart is fixed, nothing to do
	return 0;
}

static int litex_serial_probe(struct udevice *dev)
{
	// uart is fixed, nothing to do
	return 0;
}

static int litex_serial_getc(struct udevice *dev)
{
	int c;
	struct litex_uart_platdata *platdata = dev_get_platdata(dev);

	while (csr_readb(platdata->regs, LITEX_UART_RXEMPTY_REG) & 0x01)
		; // wait for rx-buffer not empty
	c = csr_readb(platdata->regs, LITEX_UART_RXTX_REG);
	csr_writeb(0x02, platdata->regs, LITEX_UART_EV_PENDING_REG);
	return c;
}

static int litex_serial_putc(struct udevice *dev, const char c)
{
	struct litex_uart_platdata *platdata = dev_get_platdata(dev);

    	while (csr_readb(platdata->regs, LITEX_UART_TXFULL_REG) & 0x01)
		; // wait while tx-buffer full
	csr_writeb(c, platdata->regs, LITEX_UART_RXTX_REG);
	return 0;
}

static int litex_serial_pending(struct udevice *dev, bool input)
{
	struct litex_uart_platdata *platdata = dev_get_platdata(dev);

	if (input)
		return !(csr_readb(platdata->regs, LITEX_UART_RXEMPTY_REG) & 0x01);
	else
		return csr_readb(platdata->regs, LITEX_UART_TXFULL_REG) & 0x01;
}

static int litex_serial_ofdata_to_platdata(struct udevice *dev)
{
	struct litex_uart_platdata *platdata = dev_get_platdata(dev);

	platdata->regs = (unsigned int *)dev_read_addr(dev);
	if (IS_ERR(platdata->regs))
		return PTR_ERR(platdata->regs);

	return 0;
}

static const struct dm_serial_ops litex_serial_ops = {
	.putc = litex_serial_putc,
	.getc = litex_serial_getc,
	.pending = litex_serial_pending,
	.setbrg = litex_serial_setbrg,
};

static const struct udevice_id litex_serial_ids[] = {
	{ .compatible = "litex,uart0" },
	{ }
};

U_BOOT_DRIVER(serial_litex) = {
	.name	= "serial_litex",
	.id	= UCLASS_SERIAL,
	.of_match = litex_serial_ids,
	.ofdata_to_platdata = litex_serial_ofdata_to_platdata,
	.platdata_auto_alloc_size = sizeof(struct litex_uart_platdata),
	.probe = litex_serial_probe,
	.ops	= &litex_serial_ops,
};

