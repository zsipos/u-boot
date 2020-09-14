// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <command.h>

#define MEMBASE      0x90000000
#define KERNEL_FILE  "sel4+linux"
#define VERSION_FILE "/root/version"

static int is_ts_pressed(void)
{
	u32 *gpioaddr;
	int  ret;

	run_command("fdt get addr tsgpio /soc/gpio@1 reg", CMD_FLAG_ENV);
	gpioaddr = (void*)env_get_hex("tsgpio", MEMBASE);
	gpioaddr = (void*)(u64)swab32(*gpioaddr);

	ret = *gpioaddr & 1 ? 0 : 1;

	printf("touch screen is");
	if (!ret) printf(" not");
	printf(" pressed\n\n");
	return ret;
}

static int get_partition_version(int partition)
{
	char  cmd[256];
	char *valstr = (char*)MEMBASE, *endp;
	int   ret = -1;

	printf("loading %s from partition %d\n", VERSION_FILE, partition);

	sprintf(cmd, "load mmc 0:%d %x %s", partition, MEMBASE, VERSION_FILE);
	if (run_command(cmd, CMD_FLAG_ENV)) {
		printf("\n");
		return -1;
	}

	valstr[env_get_hex("filesize", MEMBASE)] = 0;

	printf("content is %s\n", valstr);

	ret = simple_strtoul(valstr, &endp, 10);
	if (endp == valstr)
		ret = -1;

	return ret;
}

static int do_zsiposboot(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	char cmd[256];
	int  vers1, vers2, lower, higher, selected;

	printf("\nzsipos boot ...\n\n");

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

	printf("load kernel image ...\n");
	sprintf(cmd, "fdt set /chosen partition <%d>", selected);
	run_command(cmd, CMD_FLAG_ENV);
	sprintf(cmd, "load mmc 0:%d 0x%x %s\n", selected, MEMBASE, KERNEL_FILE);
	run_command(cmd, CMD_FLAG_ENV);
	sprintf(cmd, "bootm 0x%x - ${fdtcontroladdr}", MEMBASE);
	run_command(cmd, CMD_FLAG_ENV);

	return 0;
}

U_BOOT_CMD(
	zsiposboot,	1,	1,	do_zsiposboot,
	"boot zsipos",
	""
);
