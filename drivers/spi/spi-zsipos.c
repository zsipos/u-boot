// SPDX-License-Identifier: GPL-2.0+
/*
 * zsipos_spi driver
 *
 * Copyright (c) 2020 Stefan Adams <Stefan.Adams@vipcomag.de>
 * 
 * based on:
 *
 * OpenCores ZSIPOS_SPI driver
 *
 * http://opencores.org/project,ZSIPOS_SPI
 *
 * based on oc_tiny_spi.c which in turn was based on bfin_spi.c
 * Copyright (c) 2005-2008 Analog Devices Inc.
 * Copyright (C) 2010 Thomas Chou <thomas@wytron.com.tw>
 * Copyright (C) 2011 Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <malloc.h>
#include <spi.h>
#include <spi-mem.h>
#include <wait_bit.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <clk.h>

#define ZSIPOS_SPI_SPCR_SPIE	(1 << 7)
#define ZSIPOS_SPI_SPCR_SPE	(1 << 6)
#define ZSIPOS_SPI_SPCR_MSTR	(1 << 4)
#define ZSIPOS_SPI_SPCR_CPOL	(1 << 3)
#define ZSIPOS_SPI_SPCR_CPHA	(1 << 2)

#define ZSIPOS_SPI_SPSR_SPIF	(1 << 7)
#define ZSIPOS_SPI_SPSR_WCOL	(1 << 6)
#define ZSIPOS_SPI_SPSR_WFFULL	(1 << 3)
#define ZSIPOS_SPI_SPSR_WFEMPTY	(1 << 2)
#define ZSIPOS_SPI_SPSR_RFFULL	(1 << 1)
#define ZSIPOS_SPI_SPSR_RFEMPTY	(1 << 0)

#define ZSIPOS_SPI_SPCR_SPR	0x03
#define ZSIPOS_SPI_SPER_ESPR 	0x03

#define DUMMY_BYTE 0xff

struct zsipos_spi {
	/* regs */
	volatile u8 *spcr;
	volatile u8 *spsr;
	volatile u8 *spdr;
	volatile u8 *sper;
	volatile u8 *ssel;
	/* conf */
	u32   freq;
	u32   mode;
	u32   baud;
	u32   flg;
};

static int zsipos_spi_claim_bus(struct udevice *dev)
{
	struct zsipos_spi *spi = dev_get_priv(dev->parent);
	u8                 spcr = 0;
	u8                 sper = 0;

	/* SPI Enable */
	spcr |= ZSIPOS_SPI_SPCR_SPE;
	/* Controller only supports Master mode, but set it explicitly */
	spcr |= ZSIPOS_SPI_SPCR_MSTR;

	if (spi->mode & SPI_CPOL)
		spcr |= ZSIPOS_SPI_SPCR_CPOL;
	if (spi->mode & SPI_CPHA)
		spcr |= ZSIPOS_SPI_SPCR_CPHA;

	spcr |= (spi->baud & ZSIPOS_SPI_SPCR_SPR);
	sper |= (spi->baud >> 2) & ZSIPOS_SPI_SPER_ESPR;

	*spi->spcr = spcr;
	*spi->sper = sper;

	return 0;
}

static int zsipos_spi_release_bus(struct udevice *dev)
{
	struct zsipos_spi *spi = dev_get_priv(dev->parent);
	u8                 spcr = *spi->spcr;

	/* Disable SPI */
	spcr &= ~ZSIPOS_SPI_SPCR_SPE;
	*spi->spcr = spcr;

	return 0;
}

void spi_cs_activate(struct zsipos_spi *spi, u8 cs)
{
	u8 flags = *spi->ssel | (1 << cs);

	*spi->ssel = flags;
}

void spi_cs_deactivate(struct zsipos_spi *spi, u8 cs)
{
	u8 flags = *spi->ssel & ~(1 << cs);

	*spi->ssel = flags;
}


static int zsipos_spi_xfer(struct udevice *dev, unsigned int bitlen,
			   const void *dout, void *din, unsigned long flags)
{
	struct zsipos_spi *spi = dev_get_priv(dev->parent);
	u8                 cs;
	const u8          *txp = dout;
	u8                *rxp = din;
	uint               bytes = bitlen / 8;
	uint               rxbytes = 0;
	uint               txbytes = 0;
	int                ret = 0;
	u8                 dummy;

	cs  = ((struct dm_spi_slave_platdata *)dev_get_parent_platdata(dev))->cs;

	if (bitlen == 0)
		goto done;

	/* assume to do 8 bits transfers */
	if (bitlen % 8) {
		flags |= SPI_XFER_END;
		goto done;
	}

	if (flags & SPI_XFER_BEGIN)
		spi_cs_activate(spi, cs);

	/* empty read fifo */
	while(!(*spi->spsr & ZSIPOS_SPI_SPSR_RFEMPTY)) {
		if (ctrlc())
			return -1;
		dummy = *spi->spdr;
	}

	while(rxbytes < bytes) {
		if (!(*spi->spsr & ZSIPOS_SPI_SPSR_RFEMPTY)) {
			if (rxp)
				*rxp++ = *spi->spdr;
			else
				dummy = *spi->spdr;
			rxbytes++;
		}
		if (!(*spi->spsr & ZSIPOS_SPI_SPSR_WFFULL) && txbytes < bytes) {
			if (txp)
				*spi->spdr = *txp++;
			else
				*spi->spdr = DUMMY_BYTE;
			txbytes++;
		}
		if (ctrlc()) {
			ret = -1;
			goto done;
		}
	}

 done:
	if (flags & SPI_XFER_END)
		spi_cs_deactivate(spi, cs);

	return ret;
}

static int zsipos_spi_set_speed(struct udevice *dev, uint speed)
{
	struct zsipos_spi *spi = dev_get_priv(dev);
	int                i;

	for (i = 0; i < 11; i++) {
		if ((spi->freq >> (1+i)) <= speed) {
			break;
		}
	}

	/* The register values for some cases are weird... fix here */
	switch (i) {
	case 3:
		i = 5;
		break;
	case 4:
		i = 3;
		break;
	case 5:
		i = 4;
		break;
	}

	spi->baud = i;

	return 0;
}

static int zsipos_spi_set_mode(struct udevice *dev, uint mode)
{
	struct zsipos_spi *spi = dev_get_priv(dev);

	spi->mode = mode & (SPI_CPOL | SPI_CPHA);
	spi->flg  = mode & SPI_CS_HIGH ? 1 : 0;

	return 0;
}

static int zsipos_spi_probe(struct udevice *dev)
{
	int                ret;
	struct zsipos_spi *spi = dev_get_priv(dev);
	void              *regs = dev_remap_addr(dev);
	struct clk         clk;

	if (!regs)
		return -ENODEV;

	spi->spcr = regs;
	spi->spsr = regs + 4;
	spi->spdr = regs + 8;
	spi->sper = regs + 12;
	spi->ssel = regs + 16;

	ret = clk_get_by_index(dev, 0, &clk);
	if (ret)
		return ret;
	spi->freq = clk_get_rate(&clk);

	return 0;
}

static const struct dm_spi_ops zsipos_spi_ops = {
	.claim_bus      = zsipos_spi_claim_bus,
	.release_bus    = zsipos_spi_release_bus,
	.xfer		= zsipos_spi_xfer,
	.set_speed	= zsipos_spi_set_speed,
	.set_mode	= zsipos_spi_set_mode,
};

static const struct udevice_id zsipos_spi_ids[] = {
	{ .compatible = "zsipos,spi" },
	{ }
};

U_BOOT_DRIVER(zsipos_spi) = {
	.name	= "zsipos_spi",
	.id	= UCLASS_SPI,
	.of_match = zsipos_spi_ids,
	.ops	= &zsipos_spi_ops,
	.priv_auto_alloc_size = sizeof(struct zsipos_spi),
	.probe	= zsipos_spi_probe,
};

