/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <stdlib.h>
#include <sm_xdfu.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/modem/modem_cellular.h>

static const struct device *modem = DEVICE_DT_GET_ONE(nordic_nrf91_sm_v2);

int main(void)
{
	return 0;
}

/* Minimal wrapper around sm_xdfu_lib: parses shell arguments, runs the
 * update, and reports the outcome. All XDFU protocol handling lives in
 * lib/sm_xdfu_lib so it can also be used without a shell.
 */
static int cmd_xdfu(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	enum sm_xdfu_image_type type;
	const char *file;

	if (argc < 3) {
		shell_error(sh, "Usage: xdfu <type> <file>");
		return -EINVAL;
	}

	type = (enum sm_xdfu_image_type)atoi(argv[1]);
	file = argv[2];

	ret = sm_xdfu_run(modem, type, file);
	switch (ret) {
	case 0:
		shell_print(sh, "XDFU completed successfully");
		break;
	case -EBUSY:
		shell_error(sh, "Modem or UART is in use, cannot run XDFU");
		break;
	case -EINVAL:
		shell_error(sh, "Invalid XDFU type or file: %s", file);
		break;
	case -ENODEV:
		shell_error(sh, "Modem or UART is not ready");
		break;
	case -ETIMEDOUT:
		shell_error(sh, "Timed out waiting for modem response");
		break;
	default:
		shell_error(sh, "XDFU failed: %d", ret);
		break;
	}

	return ret;
}

SHELL_CMD_ARG_REGISTER(xdfu, NULL, "Run #XDFU on modem\n xdfu <type> <file>", cmd_xdfu, 3, 0);
