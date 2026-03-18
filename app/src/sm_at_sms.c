/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#define _POSIX_C_SOURCE 200809L /* for strdup() */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <modem/sms.h>
#include <assert.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_sms, CONFIG_SM_LOG_LEVEL);

#define MAX_CONCATENATED_MESSAGE  3

/**@brief SMS operations. */
enum sm_sms_operation {
	AT_SMS_STOP,
	AT_SMS_START,
	AT_SMS_SEND
};

static void cleanup_handler(struct k_work *work);
struct sm_sms_context {
	int sms_handle;
	struct modem_pipe *pipe;
	uint16_t ref_number;
	uint8_t total_msgs;
	uint8_t count;
	char *messages[MAX_CONCATENATED_MESSAGE];
	struct k_work_delayable cleanup_work;
} static sm_sms_ctx = {
	.sms_handle = -1,
	.cleanup_work = Z_WORK_DELAYABLE_INITIALIZER(cleanup_handler),
};

#define MSG(i, _)	ctx->messages[i]
#define MESSAGES() 	LISTIFY(MAX_CONCATENATED_MESSAGE, MSG, (,))

static const char *concatenated_fmt(int total_msgs)
{
	switch (total_msgs) {
	case 1:
		return "\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d\",\""
		       "%s\",\"%s\"\r\n";
	case 2:
		return "\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d\",\""
		       "%s\",\"%s%s\"\r\n";
	case 3:
		return "\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d\",\""
		       "%s\",\"%s%s%s\"\r\n";
	default:
		/* This should not happen */
		assert(false);
		return NULL;
	}
}



static void sms_callback(struct sms_data *const data, void *context)
{
	struct sm_sms_context *ctx = (struct sm_sms_context *)context;

	if (data == NULL) {
		LOG_WRN("NULL data");
		return;
	}

	if (data->type == SMS_TYPE_DELIVER) {
		struct sms_deliver_header *header = &data->header.deliver;

		if (!header->concatenated.present) {
			urc_send_to(ctx->pipe,
				    "\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d UTC%+03d:%02d\",\""
				    "%s\",\"%s\"\r\n",
				    header->time.year, header->time.month, header->time.day,
				    header->time.hour, header->time.minute, header->time.second,
				    header->time.timezone * 15 / 60,
				    abs(header->time.timezone) * 15 % 60,
				    header->originating_address.address_str, data->payload);
		} else {
			LOG_DBG("concatenated message %d, %d, %d",
				header->concatenated.ref_number,
				header->concatenated.total_msgs,
				header->concatenated.seq_number);
			/* ref_number and total_msgs should remain unchanged */
			if (ctx->ref_number == 0) {
				ctx->ref_number = header->concatenated.ref_number;
			}
			if (ctx->ref_number != header->concatenated.ref_number) {
				LOG_ERR("SMS concatenated message ref_number error: %d, %d",
					ctx->ref_number, header->concatenated.ref_number);
				goto done;
			}
			if (ctx->total_msgs == 0) {
				ctx->total_msgs = header->concatenated.total_msgs;
			}
			if (ctx->total_msgs != header->concatenated.total_msgs) {
				LOG_ERR("SMS concatenated message total_msgs error: %d, %d",
					ctx->total_msgs, header->concatenated.total_msgs);
				goto done;
			}
			if (ctx->total_msgs > MAX_CONCATENATED_MESSAGE) {
				LOG_ERR("SMS concatenated message no memory: %d", ctx->total_msgs);
				goto done;
			}
			/* seq_number should start with 1 but could arrive in random order */
			if (header->concatenated.seq_number == 0 ||
			    header->concatenated.seq_number > ctx->total_msgs) {
				LOG_ERR("SMS concatenated message seq_number error: %d, %d",
					header->concatenated.seq_number, ctx->total_msgs);
				goto done;
			}

			char *msg = strdup(data->payload);

			if (msg == NULL) {
				LOG_ERR("Failed to allocate memory for SMS message");
				goto done;
			}
			ctx->messages[header->concatenated.seq_number -1] = msg;
			ctx->count++;

			if (ctx->count == ctx->total_msgs) {
				urc_send_to(ctx->pipe, concatenated_fmt(ctx->total_msgs), header->time.year, header->time.month,
					    header->time.day, header->time.hour, header->time.minute,
					    header->time.second, header->originating_address.address_str,
					    MESSAGES());
			} else {
				/* Wait for more messages */
				k_work_reschedule(&ctx->cleanup_work, K_MINUTES(5));
				return;
			}
done:
			k_work_reschedule(&ctx->cleanup_work, K_NO_WAIT);
		}
	} else if (data->type == SMS_TYPE_STATUS_REPORT) {
		LOG_INF("Status report received");
	} else {
		LOG_WRN("Unknown type: %d", data->type);
	}
}

static void cleanup_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct sm_sms_context *ctx = CONTAINER_OF(dwork, struct sm_sms_context, cleanup_work);

	ctx->ref_number = 0;
	ctx->total_msgs = 0;
	ctx->count = 0;
	for (int i = 0; i < MAX_CONCATENATED_MESSAGE; i++) {
		if (ctx->messages[i]) {
			free(ctx->messages[i]);
			ctx->messages[i] = NULL;
		}
	}
}

static int do_sms_start(void)
{
	int err = 0;

	if (sm_sms_ctx.sms_handle >= 0) {
		/* already registered */
		return -EBUSY;
	}

	sm_sms_ctx.sms_handle = sms_register_listener(sms_callback, &sm_sms_ctx);
	if (sm_sms_ctx.sms_handle < 0) {
		err = sm_sms_ctx.sms_handle;
		LOG_ERR("SMS start error: %d", err);
		sm_sms_ctx.sms_handle = -1;
	}

	return err;
}

static int do_sms_stop(void)
{
	sms_unregister_listener(sm_sms_ctx.sms_handle);
	sm_sms_ctx.sms_handle = -1;

	return 0;
}

static int do_sms_send(const char *number, const char *message, uint16_t message_len)
{
	int err;

	if (sm_sms_ctx.sms_handle < 0) {
		LOG_ERR("SMS not registered");
		return -EPERM;
	}

	err = sms_send(number, message, message_len, SMS_DATA_TYPE_ASCII);
	if (err) {
		LOG_ERR("SMS send error: %d", err);
	}

	return err;
}

SM_AT_CMD_CUSTOM(xsms, "AT#XSMS", handle_at_sms);
static int handle_at_sms(enum at_parser_cmd_type cmd_type, struct at_parser *parser, uint32_t)
{
	int err = -EINVAL;
	uint16_t op;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err) {
			return err;
		}
		if (op == AT_SMS_STOP) {
			err = do_sms_stop();
		} else if (op == AT_SMS_START) {
			sm_sms_ctx.pipe = sm_at_host_get_current_pipe();
			err = do_sms_start();
		} else if (op ==  AT_SMS_SEND) {
			char number[SMS_MAX_ADDRESS_LEN_CHARS + 1];
			const char *msg_ptr = NULL;
			int size;

			size = SMS_MAX_ADDRESS_LEN_CHARS + 1;
			err = util_string_get(parser, 2, number, &size);
			if (err) {
				return err;
			}
			err = at_parser_string_ptr_get(parser, 3, &msg_ptr, &size);
			if (err) {
				return err;
			}
			err = do_sms_send(number, msg_ptr, size);
		} else {
			LOG_WRN("Unknown SMS operation: %d", op);
			err = -EINVAL;
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XSMS: (%d,%d,%d),<number>,<message>\r\n",
			AT_SMS_STOP, AT_SMS_START, AT_SMS_SEND);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}
