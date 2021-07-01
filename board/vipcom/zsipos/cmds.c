// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <command.h>
#include <linux/delay.h>

#include "litex_icape2regs.h"

#ifndef ZSIPOS_BOOT_VERSION
#warning "ZSIPOS_BOOT_VERSION not defined, defaulting to 3"
#define ZSIPOS_BOOT_VERSION	3
#endif

#define MEMBASE            0x90000000
#define KERNEL_FILE        "sel4+linux"
#define VERSION_FILE       "/versioncount"
#define ROTATE_FILE        "/rotate"
#define FIRMWARE_VERS_FILE "/img_vers"
#define FIRMWARE_FPGA_FILE "/img_fpga"
#define FIRMWARE_BOOT_FILE "/img_boot"

typedef char cmdt[256];

static u32 get_addr32(const char *path)
{
	cmdt cmd;

	sprintf(cmd, "fdt get value temp %s reg", path);
	if (run_command(cmd, CMD_FLAG_ENV)) {
		panic("%s failed", cmd);
	}
	return swab32(env_get_hex("temp", 0) >> 32);
}

static void *get_addr(const char *path)
{
	return (void*)(u64)get_addr32(path);
}

static int is_ts_pressed(void)
{
	u32 *gpioaddr = get_addr("/soc/gpio@1");
	int  ret = *gpioaddr & 1 ? 0 : 1;

	printf("touch screen is");
	if (!ret) printf(" not");
	printf(" pressed\n\n");
	return ret;
}

static int get_int_from_file(int partition, const char *filename)
{
	cmdt  cmd;
	char *valstr = (char*)MEMBASE, *endp;
	int   ret = -1;

	printf("loading %s from partition %d\n", filename, partition);

	sprintf(cmd, "load mmc 0:%d %x %s", partition, MEMBASE, filename);
	if (run_command(cmd, CMD_FLAG_ENV)) {
		printf("\n");
		return -1;
	}

	valstr[env_get_hex("filesize", 0)] = 0;

	printf("content is %s\n", valstr);

	ret = simple_strtoul(valstr, &endp, 10);
	if (endp == valstr)
		ret = -1;

	return ret;
}

static int get_partition_version(int partition)
{
	return get_int_from_file(partition, VERSION_FILE);
}

static u32 get_flash_partition_offset(int flash_partition)
{
	cmdt path;

	sprintf(path, "/soc/spi@2/spi_flash@0/partition@%d", flash_partition);
	return get_addr32(path);
}

static u32 *get_wbicape2_addr(void)
{
	return get_addr("/soc/wbicape2");
}

static void icape2_delay(void)
{
	mdelay(10);
}

static int is_fallback(void)
{
	volatile u32 *wbicape2addr = get_wbicape2_addr();
	u32 val;

	for (;;) {
		val = wbicape2addr[ICAPE2_BOOTSTS];
		if (val != 0xffffffff)
			return val & (1<<1);
		icape2_delay();
	}
	return 0;
}

static void reconfigure_fpga(void)
{
	volatile u32 *wbicape2addr = get_wbicape2_addr();

	printf("reconfigure fpga ...\n");
	mdelay(1000);
	for(;;) {
		wbicape2addr[ICAPE2_CMD] = 0xf;
		icape2_delay();
	}
}

static void flash_file_to_flash_partition(const char *filename, int sd_partition, int flash_partition, int with_len_crc)
{
	cmdt cmd;
	u32  hdr_size, offset, size;

	printf("write %d:%s to flashrom partition %d ... \n", sd_partition, filename, flash_partition);
	hdr_size = with_len_crc ? 8 : 0; //u32 len,crc32
	offset = get_flash_partition_offset(flash_partition);
	sprintf(cmd, "load mmc 0:%d %x %s", sd_partition, MEMBASE + hdr_size, filename);
	if (run_command(cmd, CMD_FLAG_ENV)) {
		panic("can not load %s to ram", filename);
	}
	size = env_get_hex("filesize", 0);
	if (with_len_crc) {
		u32 *mem = (u32*)MEMBASE;
		u64  crc = 0;
		sprintf(cmd, "crc32 0x%x 0x%x 0x%p", MEMBASE + hdr_size, size, &crc);
		if (run_command(cmd, CMD_FLAG_ENV)) {
			panic("can not calculate crc");
		}
		mem[0] = size;
		mem[1] = (u32)crc;
	}
	sprintf(cmd, "sf update 0x%x 0x%x 0x%x", MEMBASE, offset, size + hdr_size);
	if (run_command(cmd, CMD_FLAG_ENV)) {
		panic("can not update flashrom");
	}
}

static void check_firmware_version(int partition)
{
	int  version;

	printf("\n");
	version = get_int_from_file(partition, FIRMWARE_VERS_FILE);
	if (version == -1) {
		panic("no %s found", FIRMWARE_VERS_FILE);
	}
	printf("partition flashrom version is %d, current flashrom version is %d\n\n", version, ZSIPOS_BOOT_VERSION);
	if ((version != ZSIPOS_BOOT_VERSION) || is_fallback()) {
		if (is_fallback())
			printf("flashrom corruption detected, forcing flashrom update\n");
		printf("flashrom update to version %d ...\n", version);
		run_command("sf probe", CMD_FLAG_ENV);
		flash_file_to_flash_partition(FIRMWARE_FPGA_FILE, partition, 2, 0);
		flash_file_to_flash_partition(FIRMWARE_BOOT_FILE, partition, 3, 1);
		reconfigure_fpga();
	}
}

static void check_rotation(int partition)
{
	if (get_int_from_file(partition, ROTATE_FILE) > 0) {
		printf("display is upside down.\n\n");
		run_command("fdt set /soc/spi@1/ws35a_display@0 rotate <270>", CMD_FLAG_ENV);
	}
}

static int do_zsiposboot(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	cmdt cmd;
	int  vers1, vers2, lower, higher, selected;

	printf("\nzsipos boot version %d ...\n\n", ZSIPOS_BOOT_VERSION);

	// select boot fdt
	run_command("fdt addr ${fdtcontroladdr}", CMD_FLAG_ENV);

	vers1 = get_partition_version(1);
	vers2 = get_partition_version(2);

	if (vers1 < vers2) {
		lower  = 1;
		higher = 2;
	} else {
		lower  = 2;
		higher = 1;
	}

	if (is_ts_pressed())
		selected = lower;
	else
		selected = higher;

	printf("select partition %d\n", selected);

	check_firmware_version(selected);
	check_rotation(selected);

	printf("load kernel image ...\n");
	sprintf(cmd, "fdt set /chosen zsipos,partition %d", selected);
	run_command(cmd, CMD_FLAG_ENV);
	sprintf(cmd, "fdt set /chosen zsipos,boot-version %d", ZSIPOS_BOOT_VERSION);
	run_command(cmd, CMD_FLAG_ENV);
	sprintf(cmd, "load mmc 0:%d 0x%x %s\n", selected, MEMBASE, KERNEL_FILE);
	if (run_command(cmd, CMD_FLAG_ENV)) {
		fprintf(stderr, "can not load kernel!\n");
	} else {
		sprintf(cmd, "bootm 0x%x - ${fdtcontroladdr}", MEMBASE);
		run_command(cmd, CMD_FLAG_ENV);
	}

	return 0;
}

U_BOOT_CMD(
	zsiposboot,	1,	1,	do_zsiposboot,
	"boot zsipos",
	""
);
