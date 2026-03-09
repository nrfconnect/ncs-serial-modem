/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modem/sms.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_sms, CONFIG_SM_LOG_LEVEL);

#define MAX_CONCATENATED_MESSAGE 10
#define SM_SMS_AT_HEADER_INFO_MAX_LEN 64
/* Maximum time to wait for the next part of the concatenated message to be received in minutes */
#define MAX_CONCATENATED_MESSAGE_AGE K_MINUTES(3)

/**@brief SMS operations. */
enum sm_sms_operation {
	AT_SMS_STOP,
	AT_SMS_START,
	AT_SMS_SEND
};

static void sms_concat_cleanup_work_fn(struct k_work *work);

struct sm_sms_context {
	int sms_handle;
	struct modem_pipe *pipe; /* Pipe requesting SMS service */
	uint16_t ref_number; /* Reference number in the concatenated message */
	uint8_t total_msgs; /* Total number of messages in the concatenated message */
	uint8_t count; /* Current number of messages received in the concatenated message */
	char *concat_rsp_buf; /* Buffer for the concatenated message */
	struct k_work_delayable cleanup_work;
};

static struct sm_sms_context sms_ctx = {
	.sms_handle = -1,
	.concat_rsp_buf = NULL,
	.cleanup_work = Z_WORK_DELAYABLE_INITIALIZER(sms_concat_cleanup_work_fn),
};

static void sms_concat_clear(struct sm_sms_context *ctx)
{
	k_work_cancel_delayable(&ctx->cleanup_work);
	if (ctx->concat_rsp_buf != NULL) {
		free(ctx->concat_rsp_buf);
		ctx->concat_rsp_buf = NULL;
	}
	ctx->ref_number = 0;
	ctx->total_msgs = 0;
	ctx->count = 0;
}

static void sms_concat_cleanup_work_fn(struct k_work *work)
{
	struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
	struct sm_sms_context *ctx = CONTAINER_OF(dwork, struct sm_sms_context, cleanup_work);

	sms_concat_clear(ctx);
	LOG_INF("Concat msg timed out, ref_number %u", ctx->ref_number);
}

static void sms_concat_handle(struct sms_data *const data)
{
	struct sms_deliver_header *header = &data->header.deliver;

	LOG_DBG("Concat msg %d, %d, %d",
		header->concatenated.ref_number,
		header->concatenated.total_msgs,
		header->concatenated.seq_number);

	/* ref_number and total_msgs should remain unchanged.
	 * If they are different, this is a different concatenated message
	 * and we discard the current and try with the new message.
	 * We may end up in changing from one message to another one if two
	 * concatenated messages are received at the same time in mixed order.
	 */
	if (sms_ctx.ref_number != 0 && sms_ctx.ref_number != header->concatenated.ref_number) {
		LOG_ERR("Concat msg ref_number error: %d, %d",
			sms_ctx.ref_number, header->concatenated.ref_number);
		sms_concat_clear(&sms_ctx);
	}
	if (sms_ctx.ref_number == 0) {
		sms_ctx.ref_number = header->concatenated.ref_number;

		if (header->concatenated.total_msgs > MAX_CONCATENATED_MESSAGE) {
			LOG_WRN("Ignoring concat msg with %d messages (max: %d)",
				header->concatenated.total_msgs, MAX_CONCATENATED_MESSAGE);
			goto done;
		}

		/* Allocate buffer for concatenated message. The allocation
		 * size is an upper boundary as headers and last message part
		 * are slightly less in practice.
		 */
		uint16_t concat_msg_len =
			SM_SMS_AT_HEADER_INFO_MAX_LEN +
			SMS_MAX_PAYLOAD_LEN_CHARS * header->concatenated.total_msgs;
		sms_ctx.concat_rsp_buf = calloc(1, concat_msg_len);
		if (sms_ctx.concat_rsp_buf == NULL) {
			LOG_ERR("Concat msg no memory for %d bytes, %d messages",
				concat_msg_len, header->concatenated.total_msgs);
			goto done;
		}
	}
	if (sms_ctx.total_msgs == 0) {
		sms_ctx.total_msgs = header->concatenated.total_msgs;
	}
	if (sms_ctx.total_msgs != header->concatenated.total_msgs) {
		LOG_ERR("Concat msg total_msgs error: %d, %d",
			sms_ctx.total_msgs, header->concatenated.total_msgs);
		goto done;
	}
	/* seq_number should start with 1 but could arrive in random order */
	if (header->concatenated.seq_number == 0 ||
	    header->concatenated.seq_number > sms_ctx.total_msgs) {
		LOG_ERR("Concat msg seq_number error: %d, %d",
			header->concatenated.seq_number, sms_ctx.total_msgs);
		goto done;
	}
	if (header->concatenated.seq_number == 1) {
		sprintf(sms_ctx.concat_rsp_buf,
			"\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d "
			"UTC%+03d:%02d\",\"%s\",\"%s\"",
			header->time.year, header->time.month, header->time.day,
			header->time.hour, header->time.minute, header->time.second,
			header->time.timezone * 15 / 60,
			abs(header->time.timezone) * 15 % 60,
			header->originating_address.address_str,
			data->payload);
	} else {
		strcpy(sms_ctx.concat_rsp_buf + SM_SMS_AT_HEADER_INFO_MAX_LEN +
		       (header->concatenated.seq_number - 1) * SMS_MAX_PAYLOAD_LEN_CHARS,
		       data->payload);
	}
	sms_ctx.count++;
	if (sms_ctx.count == sms_ctx.total_msgs) {
		for (int i = 1; i < (sms_ctx.total_msgs); i++) {
			strncat(sms_ctx.concat_rsp_buf,
				sms_ctx.concat_rsp_buf + SM_SMS_AT_HEADER_INFO_MAX_LEN +
				i * SMS_MAX_PAYLOAD_LEN_CHARS,
				SMS_MAX_PAYLOAD_LEN_CHARS);
		}
		strcat(sms_ctx.concat_rsp_buf, "\"\r\n");
		urc_send_to(sms_ctx.pipe, "%s", sms_ctx.concat_rsp_buf);
	} else {
		/* If new messages for the concatenated message are not received
		 * within 3 minutes, discard the concatenated message.
		 */
		(void)k_work_reschedule(&sms_ctx.cleanup_work, MAX_CONCATENATED_MESSAGE_AGE);
		return;
	}
done:
	sms_concat_clear(&sms_ctx);
}

static void sms_callback(struct sms_data *const data, void *context)
{
	ARG_UNUSED(context);

	if (data == NULL) {
		return;
	}

	if (data->type == SMS_TYPE_DELIVER) {
		struct sms_deliver_header *header = &data->header.deliver;

		if (!header->concatenated.present) {
			urc_send_to(sms_ctx.pipe,
				"\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d UTC%+03d:%02d\",\""
				"%s\",\"%s\"\r\n",
				header->time.year, header->time.month, header->time.day,
				header->time.hour, header->time.minute, header->time.second,
				header->time.timezone * 15 / 60,
				abs(header->time.timezone) * 15 % 60,
				header->originating_address.address_str, data->payload);
		} else {
			sms_concat_handle(data);
		}
	} else {
		LOG_WRN("Unknown type: %d", data->type);
	}
}

static int do_sms_start(void)
{
	int err = 0;

	if (sms_ctx.sms_handle >= 0) {
		/* already registered */
		return -EBUSY;
	}

	sms_ctx.sms_handle = sms_register_listener(sms_callback, NULL);
	if (sms_ctx.sms_handle < 0) {
		err = sms_ctx.sms_handle;
		LOG_ERR("Start error: %d", err);
		sms_ctx.sms_handle = -1;
	}

	return err;
}

static int do_sms_stop(void)
{
	sms_unregister_listener(sms_ctx.sms_handle);
	sms_ctx.sms_handle = -1;
	sms_concat_clear(&sms_ctx);

	return 0;
}

static int do_sms_send(const char *number, const char *message, uint16_t message_len)
{
	int err;

	if (sms_ctx.sms_handle < 0) {
		LOG_ERR("Not registered");
		return -EPERM;
	}

	err = sms_send(number, message, message_len, SMS_DATA_TYPE_ASCII);
	if (err) {
		LOG_ERR("Send error: %d", err);
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
			sms_ctx.pipe = sm_at_host_get_current_pipe();
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
			LOG_WRN("Unknown operation: %d", op);
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
