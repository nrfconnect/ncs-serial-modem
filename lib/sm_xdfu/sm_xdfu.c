/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <sm_xdfu.h>
#include <zephyr/modem/chat.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/drivers/modem/modem_cellular.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/fs/fs.h>
#include <modem_update_decode.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sm_xdfu_lib, CONFIG_SM_XDFU_LIB_LOG_LEVEL);

#define XDFU_CHAT_RECEIVE_BUF_SIZE 1024
#define XDFU_CHAT_ARGV_COUNT       4
#define XDFU_CMD_MAX_LEN 256
#define XDFU_MAX_CHUNK_SIZE 4096

/* The size of the cbor metadata structure will not exceed this value. */
#define MAX_META_LEN 1024

#define XDFU_DATA_WRITE_TIMEOUT K_SECONDS(10)
#define XDFU_APPLY_TIMEOUT      K_SECONDS(30)

static void xdfu_chat_on_xdfu(struct modem_chat *chat, char **argv, uint16_t argc,
			       void *user_data);
static void xdfu_chat_on_blmode(struct modem_chat *chat, char **argv, uint16_t argc,
			       void *user_data);
static void xdfu_chat_on_ready(struct modem_chat *chat, char **argv, uint16_t argc,
			       void *user_data);

MODEM_CELLULAR_COMMON_CHAT_MATCHES();
MODEM_CELLULAR_UNSOL_DEFINE(xdfu_unsol,
	MODEM_CHAT_MATCH("#XDATAMODE: ", ",", NULL),
	MODEM_CHAT_MATCH("#XDFU:", ",", xdfu_chat_on_xdfu),
	MODEM_CHAT_MATCH("Bootloader mode ready", NULL, xdfu_chat_on_blmode),
	MODEM_CHAT_MATCH("Ready", NULL, xdfu_chat_on_ready));

K_EVENT_DEFINE(xdfu_event);

/** Events from the #XDFU URC
 * See enum xdfu_operation from sm_at_dfu.c for possible values that need to match.
 */
enum xdfu_events {
	DATA_WRITE_COMPLETE = 1,
	APPLY_UPDATE_COMPLETE = 2,

	/* private events that are not on #XDFU: %d,%d URC messages */
	BOOTLOADER_MODE_READY = 0xa,
	FAILURE,
	READY,
};

struct xdfu_chat_instance {
	struct modem_chat chat;
	uint8_t receive_buf[XDFU_CHAT_RECEIVE_BUF_SIZE];
	uint8_t cmd_buf[XDFU_CMD_MAX_LEN];
	uint8_t *argv[XDFU_CHAT_ARGV_COUNT];
	struct modem_pipe *pipe;
};

static struct xdfu_chat_instance *xdfu_chat_instance;

#define PIPE_EVENT_TRANSMIT_IDLE_BIT BIT(3)

static void wait_for_pipe_event(struct modem_pipe *pipe, uint32_t events)
{
	k_event_wait(&pipe->event, events, false, K_SECONDS(1));
}

static bool wait_for_dfu_event(enum xdfu_events event, k_timeout_t timeout)
{
	uint32_t result = k_event_wait(&xdfu_event, BIT(event) | BIT(FAILURE), false, timeout);

	if (result & BIT(FAILURE)) {
		k_event_post(&xdfu_event, BIT(FAILURE));
	}
	return result == BIT(event);
}

static bool dfu_failed(void)
{
	return k_event_test(&xdfu_event, BIT(FAILURE)) == BIT(FAILURE);
}

/**
 * Allocate buffers for chat instance
 */
static int xdfu_chat_init(void)
{
	xdfu_chat_instance = calloc(1, sizeof(struct xdfu_chat_instance));
	if (!xdfu_chat_instance) {
		LOG_ERR("Failed to allocate xdfu_chat_instance");
		return -ENOMEM;
	}

	const struct modem_chat_config xdfu_chat_config = {
		.receive_buf = xdfu_chat_instance->receive_buf,
		.receive_buf_size = XDFU_CHAT_RECEIVE_BUF_SIZE,
		.delimiter = "\r",
		.delimiter_size = sizeof("\r") - 1,
		.filter = "\n",
		.filter_size = sizeof("\n") - 1,
		.argv = xdfu_chat_instance->argv,
		.argv_size = XDFU_CHAT_ARGV_COUNT,
		.unsol_matches = xdfu_unsol.matches,
		.unsol_matches_size = xdfu_unsol.size,
		.user_data = NULL,
	};

	modem_chat_init(&xdfu_chat_instance->chat, &xdfu_chat_config);
	return 0;
}

static void xdfu_chat_on_xdfu(struct modem_chat *chat, char **argv, uint16_t argc,
			       void *user_data)
{
	ARG_UNUSED(chat);
	ARG_UNUSED(user_data);

	if (argc < 4) {
		return;
	}

	int operation = atoi(argv[2]);
	int status = atoi(argv[3]);

	if (status == 0 &&
	    (operation == DATA_WRITE_COMPLETE || operation == APPLY_UPDATE_COMPLETE)) {
		k_event_post(&xdfu_event, BIT(operation));
	} else {
		k_event_post(&xdfu_event, BIT(FAILURE));
	}
}

static void xdfu_chat_on_blmode(struct modem_chat *chat, char **argv, uint16_t argc,
			       void *user_data)
{
	ARG_UNUSED(chat);
	ARG_UNUSED(argv);
	ARG_UNUSED(argc);
	ARG_UNUSED(user_data);

	k_event_post(&xdfu_event, BIT(BOOTLOADER_MODE_READY));
}

static void xdfu_chat_on_ready(struct modem_chat *chat, char **argv, uint16_t argc,
			       void *user_data)
{
	ARG_UNUSED(chat);
	ARG_UNUSED(argv);
	ARG_UNUSED(argc);
	ARG_UNUSED(user_data);

	k_event_post(&xdfu_event, BIT(READY));
}

/**
 * Write buffer into a modem_pipe in a blocking mode.
 *
 * Monitor pipe's internal event for waiting the idle event without using any callback.
 */
static int pipe_write_blocking(struct modem_pipe *pipe, const uint8_t *buf, size_t size)
{
	int ret;

	if (!pipe || !buf || size == 0) {
		return -EINVAL;
	}

	while (size > 0) {
		ret = modem_pipe_transmit(pipe, buf, size);
		if (ret == size) {
			return 0;
		}
		if (ret < 0) {
			LOG_ERR("Failed to write to pipe: %d", ret);
			return ret;
		}
		buf += ret;
		size -= ret;
		if (ret == 0 && size > 0) {
			wait_for_pipe_event(pipe, PIPE_EVENT_TRANSMIT_IDLE_BIT);
		}
	}

	return -EIO;
}

/**
 * Write section from file to the modem_pipe.
 * @param pipe Target pipe
 * @param file File path
 * @param offset Offset within the file to read from
 * @param len length of data to write
 * @return 0 on success or negative error code on failure
 */
static int pipe_write_file(struct modem_pipe *pipe, const char *file, size_t offset, size_t len)
{
	int ret;
	uint8_t buf[256];
	size_t bytes_written = 0;
	struct fs_file_t fd;

	fs_file_t_init(&fd);

	ret = fs_open(&fd, file, FS_O_READ);
	if (ret < 0) {
		LOG_ERR("Failed to open file %s: %d", file, ret);
		return ret;
	}

	ret = fs_seek(&fd, offset, FS_SEEK_SET);
	if (ret < 0) {
		LOG_ERR("Failed to seek file %s: %d", file, ret);
		fs_close(&fd);
		return ret;
	}

	while (bytes_written < len) {
		size_t chunk_size = MIN(sizeof(buf), len - bytes_written);

		ret = fs_read(&fd, buf, chunk_size);
		if (ret < 0) {
			LOG_ERR("Failed to read file %s: %d", file, ret);
			fs_close(&fd);
			return ret;
		}
		if (ret == 0) {
			break;
		}
		bytes_written += ret;
		ret = pipe_write_blocking(pipe, buf, ret);
		if (ret < 0) {
			LOG_ERR("Failed to write to pipe: %d", ret);
			fs_close(&fd);
			return ret;
		}
	}

	fs_close(&fd);
	return 0;
}

/**
 * Run a single AT command using chat module.
 *
 * Wait for OK response.
 *
 * @param fmt AT command format string
 * @return 0 on success or negative error code on failure
 */
static int chat_cmd(struct xdfu_chat_instance *inst, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf((char *) inst->cmd_buf, sizeof(inst->cmd_buf), fmt, args);
	va_end(args);

	struct modem_chat_script_chat cmd = {
		.request = inst->cmd_buf,
		.request_size = strlen((char *)inst->cmd_buf),
		.response_matches = &ok_match,
		.response_matches_size = 1,
		.timeout = 1000,
	};

	struct modem_chat_script script = {
		.name = "xdfu_chat_cmd",
		.script_chats = &cmd,
		.script_chats_size = 1,
		.abort_matches = abort_matches,
		.abort_matches_size = ARRAY_SIZE(abort_matches),
		.callback = NULL,
		.timeout = 10,
	};

	LOG_DBG("%s", inst->cmd_buf);

	return modem_chat_run_script(&inst->chat, &script);
}

static bool sm_xdfu_type_valid(enum sm_xdfu_image_type type)
{
	switch (type) {
	case SM_XDFU_TYPE_APP:
	case SM_XDFU_TYPE_DELTA_MFW:
	case SM_XDFU_TYPE_FULL_MFW:
	case SM_XDFU_TYPE_MCUBOOT_BL:
		return true;
	default:
		return false;
	}
}

/**
 * Write sections from file to the modem using AT#XDFUWRITE.
 *
 * @param addr Target address
 * @param size Write length
 * @param type Image type
 * @param file File name to read from
 * @param file_offset Offset inside a file to read from
 * @return 0 on success or negative error code on failure.
 */
static int xdfu_write_file(size_t addr, size_t size, enum sm_xdfu_image_type type, const char *file,
			   size_t file_offset)
{
	int ret;

	for (size_t offset = 0; offset < size;) {
		k_event_clear(&xdfu_event, BIT(DATA_WRITE_COMPLETE) | BIT(FAILURE));

		size_t chunk_size = MIN(size - offset, XDFU_MAX_CHUNK_SIZE);

		int percent = (100 * (offset + chunk_size)) / size;

		LOG_INF("Writing %zu / %zu bytes (%d %%)", offset + chunk_size, size, percent);

		ret = chat_cmd(xdfu_chat_instance, "AT#XDFUWRITE=%d,%zu,%zu", type, addr,
			       chunk_size);
		if (ret < 0) {
			LOG_ERR("Failed to run XDFUWRITE: %d", ret);
			return ret;
		}

		ret = pipe_write_file(xdfu_chat_instance->pipe, file, file_offset + offset,
				      chunk_size);
		if (ret < 0) {
			LOG_ERR("Failed to write chunk: %d", ret);
			return ret;
		}

		if (!wait_for_dfu_event(DATA_WRITE_COMPLETE, XDFU_DATA_WRITE_TIMEOUT)) {
			if (dfu_failed()) {
				LOG_ERR("DFU failed during data write");
				ret = -EIO;
			} else {
				LOG_ERR("Timeout waiting for DATA_WRITE_COMPLETE event");
				ret = -ETIMEDOUT;
			}
			return ret;
		}

		offset += chunk_size;
		addr += chunk_size;
	}

	return 0;
}

static int xdfu_apply(enum sm_xdfu_image_type type)
{
	int ret;

	k_event_clear(&xdfu_event, BIT(APPLY_UPDATE_COMPLETE) | BIT(FAILURE));

	ret = chat_cmd(xdfu_chat_instance, "AT#XDFUAPPLY=%d", type);
	if (ret < 0) {
		LOG_ERR("Failed to run XDFUAPPLY: %d", ret);
		return ret;
	}
	if (!wait_for_dfu_event(APPLY_UPDATE_COMPLETE, XDFU_APPLY_TIMEOUT)) {
		if (dfu_failed()) {
			LOG_ERR("DFU failed during apply");
			ret = -EIO;
		} else {
			LOG_ERR("Timeout waiting for APPLY_UPDATE_COMPLETE event");
			ret = -ETIMEDOUT;
		}
		return ret;
	}

	return 0;
}

/**
 * Extract full-modem firmware from the CBOR structure
 * and write it to the modem.
 * See fmfu_fdev.c from nRF Connect SDK for implementation details.
 */
static int xdfu_fmfu_from_file(const char *file)
{
	const struct zcbor_string *segments_string;
	struct COSE_Sign1_Manifest wrapper;
	struct Segments segments;
	size_t file_offset;
	struct fs_file_t fd;
	uint8_t *meta_buf;
	int ret;

	meta_buf = malloc(MAX_META_LEN);
	if (!meta_buf) {
		LOG_ERR("Failed to allocate metadata buffer");
		return -ENOMEM;
	}

	fs_file_t_init(&fd);

	ret = fs_open(&fd, file, FS_O_READ);
	if (ret < 0) {
		LOG_ERR("Failed to open file %s: %d", file, ret);
		ret = -ENOMEM;
		goto out;
	}

	ret = fs_read(&fd, meta_buf, MAX_META_LEN);
	if (ret < 0) {
		LOG_ERR("Failed to read file %s: %d", file, ret);
		fs_close(&fd);
		ret = -EIO;
		goto out;
	}
	fs_close(&fd);

	if (cbor_decode_Wrapper(meta_buf, MAX_META_LEN, &wrapper, &file_offset) != ZCBOR_SUCCESS) {
		LOG_ERR("Unable to decode wrapper");
		ret = -EINVAL;
		goto out;
	}

	/* Get a pointer to, and decode,  the segments as this is a cbor encoded
	 * structure in itself.
	 */
	segments_string = &wrapper.COSE_Sign1_Manifest_payload_cbor.Manifest_segments;
	if (cbor_decode_Segments(segments_string->value, segments_string->len, &segments, NULL) !=
	    ZCBOR_SUCCESS) {
		LOG_ERR("Unable to decode segments");
		ret = -EINVAL;
		goto out;
	}

	/* Write segments to the modem. */
	for (int i = 0; i < segments.Segments_Segment_m_count; i++) {
		size_t seg_size = segments.Segments_Segment_m[i].Segment_len;
		uint32_t seg_addr = segments.Segments_Segment_m[i].Segment_target_addr;

		LOG_INF("Writing segment %d/%d size: %zu", i + 1,
			segments.Segments_Segment_m_count, seg_size);

		ret = xdfu_write_file(seg_addr, seg_size, SM_XDFU_TYPE_FULL_MFW, file, file_offset);
		if (ret < 0) {
			LOG_ERR("Failed to write segment %d: %d", i + 1, ret);
			goto out;
		}

		/* First segment is the bootloader, needs a separate apply */
		if (i == 0) {
			ret = xdfu_apply(SM_XDFU_TYPE_FULL_MFW);
			if (ret < 0) {
				LOG_ERR("Failed to apply bootloader");
				goto out;
			}
		}

		file_offset += seg_size;
	}

	/* This reboots the modem into a new FW */
	ret = xdfu_apply(SM_XDFU_TYPE_FULL_MFW);
	if (ret < 0) {
		LOG_ERR("Failed to apply firmware");
		goto out;
	}
out:
	free(meta_buf);
	return ret;
}

int sm_xdfu_run(const struct device *modem, enum sm_xdfu_image_type type, const char *file)
{
	int ret;
	struct modem_cellular_data *data;
	struct modem_cellular_config *config;
	struct fs_dirent entry;

	if (!modem || !file || !sm_xdfu_type_valid(type)) {
		return -EINVAL;
	}

	data = (struct modem_cellular_data *)modem->data;
	config = (struct modem_cellular_config *)modem->config;

	ret = fs_stat(file, &entry);
	if (ret < 0) {
		LOG_ERR("Failed to stat file: %d", ret);
		return ret;
	}

	if (entry.type != FS_DIR_ENTRY_FILE) {
		LOG_ERR("Not a file: %s", file);
		return -EINVAL;
	}

	if (entry.size == 0) {
		LOG_ERR("File is empty: %s", file);
		return -EINVAL;
	}
	LOG_INF("File %s size: %zu", file, entry.size);

	if (!device_is_ready(modem)) {
		LOG_ERR("Modem is not ready");
		return -ENODEV;
	}

	/* Ensure that modem is not used */
	if (pm_device_runtime_usage(modem) > 0) {
		LOG_ERR("Modem is in use, cannot run XDFU");
		return -EBUSY;
	}

	/* Ensure that UART is free */
	if (!device_is_ready(config->uart)) {
		LOG_ERR("UART is not ready");
		return -ENODEV;
	}

	if (pm_device_runtime_usage(config->uart) > 0) {
		LOG_ERR("UART is in use, cannot run XDFU");
		return -EBUSY;
	}

	ret = xdfu_chat_init();
	if (ret < 0) {
		LOG_ERR("Failed to initialize XDFU chat: %d", ret);
		return ret;
	}

	/* Attach to the UART pipe from modem */
	xdfu_chat_instance->pipe = data->uart_pipe;
	modem_pipe_release(data->uart_pipe);
	ret = modem_pipe_open(data->uart_pipe, K_SECONDS(1));
	if (ret < 0) {
		LOG_ERR("Failed to open UART pipe: %d", ret);
		goto release_mem;
	}

	k_event_clear(&xdfu_event, BIT(DATA_WRITE_COMPLETE) | BIT(APPLY_UPDATE_COMPLETE) |
				    BIT(FAILURE) | BIT(BOOTLOADER_MODE_READY) | BIT(READY));

	modem_chat_attach(&xdfu_chat_instance->chat, data->uart_pipe);

	if (type == SM_XDFU_TYPE_FULL_MFW) {
		ret = chat_cmd(xdfu_chat_instance, "AT#XDFUINIT=%d", type);
		if (ret < 0) {
			LOG_ERR("Failed to run XDFUINIT: %d", ret);
			goto cleanup;
		}

		LOG_INF("Waiting for bootloader mode ready...");
		if (!wait_for_dfu_event(BOOTLOADER_MODE_READY, K_SECONDS(5))) {
			LOG_ERR("Timeout waiting for bootloader mode ready");
			ret = -ETIMEDOUT;
			goto cleanup;
		}
		ret = xdfu_fmfu_from_file(file);
	} else {
		ret = chat_cmd(xdfu_chat_instance, "AT#XDFUINIT=%d,%zu", type, entry.size);
		if (ret < 0) {
			LOG_ERR("Failed to run XDFUINIT: %d", ret);
			goto cleanup;
		}
		ret = xdfu_write_file(0, entry.size, type, file, 0);
		if (ret < 0) {
			LOG_ERR("Failed to write file: %d", ret);
			goto cleanup;
		}
		ret = xdfu_apply(type);
	}

	if (ret == 0) {
		LOG_INF("XDFU completed successfully");
	}

cleanup:
	/* Only reset on Full-modem DFU if it fails */
	if (type != SM_XDFU_TYPE_FULL_MFW || ret < 0) {
		int reset_ret = chat_cmd(xdfu_chat_instance, "AT#XRESET");

		if (reset_ret < 0) {
			LOG_ERR("Failed to run XRESET: %d", reset_ret);
			if (ret == 0) {
				ret = reset_ret;
			}
		}
	}

	LOG_INF("Waiting for modem ready...");
	if (!wait_for_dfu_event(READY, K_SECONDS(30))) {
		LOG_ERR("Timeout waiting for modem ready");
		if (ret == 0) {
			ret = -ETIMEDOUT;
		}
	}

	modem_chat_release(&xdfu_chat_instance->chat);
	modem_pipe_close(data->uart_pipe, K_SECONDS(1));

release_mem:
	free(xdfu_chat_instance);
	return ret;
}
