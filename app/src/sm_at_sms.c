/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <modem/sms.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_sms, CONFIG_SM_LOG_LEVEL);

#define MAX_CONCATENATED_MESSAGE 10
#define SM_SMS_AT_HEADER_INFO_MAX_LEN 64
#define MAX_CONCATENATED_MESSAGE_AGE 3000

/**@brief SMS operations. */
enum sm_sms_operation {
	AT_SMS_STOP,
	AT_SMS_START,
	AT_SMS_SEND
};

static int sms_handle = -1;

static void sms_callback(struct sms_data *const data, void *context)
{
	static uint16_t ref_number;
	static uint8_t total_msgs;
	static uint8_t count;
	static int64_t last_received_uptime;
	static char rsp_buf[SMS_MAX_PAYLOAD_LEN_CHARS + SM_SMS_AT_HEADER_INFO_MAX_LEN] = {0};
	static char *concat_rsp_buf = NULL;
	int64_t uptime = k_uptime_get();

	ARG_UNUSED(context);

	if (data == NULL) {
		LOG_WRN("NULL data");
		return;
	}

	if (data->type == SMS_TYPE_DELIVER) {
		struct sms_deliver_header *header = &data->header.deliver;

		if (!header->concatenated.present) {
			sprintf(rsp_buf,
				"\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d UTC%+03d:%02d\",\"",
				header->time.year, header->time.month, header->time.day,
				header->time.hour, header->time.minute, header->time.second,
				header->time.timezone * 15 / 60,
				abs(header->time.timezone) * 15 % 60);
			strcat(rsp_buf, header->originating_address.address_str);
			strcat(rsp_buf, "\",\"");
			strcat(rsp_buf, data->payload);
			strcat(rsp_buf, "\"\r\n");
			rsp_send("%s", rsp_buf);
			if (concat_rsp_buf != NULL && uptime - last_received_uptime > MAX_CONCATENATED_MESSAGE_AGE) {
				LOG_WRN("Cleaned SMS ref number %d, last_received_uptime=%lld, uptime=%lld",
					ref_number, last_received_uptime, uptime);
				ref_number = 0;
				total_msgs = 0;
				count = 0;
				if (concat_rsp_buf != NULL) {
					free(concat_rsp_buf);
					concat_rsp_buf = NULL;
				}
				last_received_uptime = 0;
			}
		} else {
			LOG_DBG("concatenated message %d, %d, %d",
				header->concatenated.ref_number,
				header->concatenated.total_msgs,
				header->concatenated.seq_number);

			if (last_received_uptime != 0 &&
			    ref_number != header->concatenated.ref_number &&
			    uptime - last_received_uptime > MAX_CONCATENATED_MESSAGE_AGE) {
				LOG_WRN("Cleaned SMS ref number %d, last_received_uptime=%lld, uptime=%lld",
					ref_number, last_received_uptime, uptime);
				ref_number = 0;
				total_msgs = 0;
				count = 0;
				if (concat_rsp_buf != NULL) {
					free(concat_rsp_buf);
					concat_rsp_buf = NULL;
				}
				last_received_uptime = 0;
			}

			/* ref_number and total_msgs should remain unchanged */
			if (ref_number == 0) {
				ref_number = header->concatenated.ref_number;
				
				if (header->concatenated.total_msgs > MAX_CONCATENATED_MESSAGE) {
					LOG_ERR("SMS concatenated message limited to %d, received: %d",
						MAX_CONCATENATED_MESSAGE, header->concatenated.total_msgs);
					goto done;
				}
	
				/* Allocate buffer for concatenated message. The allocation
				 * size is an upper boundary as headers and last message part
				 * are slightly less in practice.
				 */
				uint16_t concat_msg_len =
					SM_SMS_AT_HEADER_INFO_MAX_LEN +
					SMS_MAX_PAYLOAD_LEN_CHARS * header->concatenated.total_msgs;
				concat_rsp_buf = malloc(concat_msg_len);
				if (concat_rsp_buf == NULL) {
					LOG_ERR("SMS concatenated message no memory for "
						"%d bytes, %d messages",
						concat_msg_len,
						header->concatenated.total_msgs);
					goto done;
				}
				memset(concat_rsp_buf, 0, concat_msg_len);
			}
			if (ref_number != header->concatenated.ref_number) {
				LOG_ERR("SMS concatenated message ref_number error: %d, %d",
					ref_number, header->concatenated.ref_number);
				goto done;
			}
			if (total_msgs == 0) {
				total_msgs = header->concatenated.total_msgs;
			}
			if (total_msgs != header->concatenated.total_msgs) {
				LOG_ERR("SMS concatenated message total_msgs error: %d, %d",
					total_msgs, header->concatenated.total_msgs);
				goto done;
			}
			last_received_uptime = uptime;
			/* seq_number should start with 1 but could arrive in random order */
			if (header->concatenated.seq_number == 0 ||
			    header->concatenated.seq_number > total_msgs) {
				LOG_ERR("SMS concatenated message seq_number error: %d, %d",
					header->concatenated.seq_number, total_msgs);
				goto done;
			}
			if (header->concatenated.seq_number == 1) {
				sprintf(concat_rsp_buf,
					"\r\n#XSMS: \"%02d-%02d-%02d %02d:%02d:%02d\",\"",
					header->time.year, header->time.month, header->time.day,
					header->time.hour, header->time.minute,
					header->time.second);
				strcat(concat_rsp_buf, header->originating_address.address_str);
				strcat(concat_rsp_buf, "\",\"");
				strcat(concat_rsp_buf, data->payload);
				//LOG_WRN("concat_rsp_buf (seq_number=1): %s", concat_rsp_buf);
			} else {
				strcpy(concat_rsp_buf + SM_SMS_AT_HEADER_INFO_MAX_LEN + (header->concatenated.seq_number - 1) * SMS_MAX_PAYLOAD_LEN_CHARS,
				       data->payload);
				//LOG_WRN("concat_rsp_buf (seq_number=%d): %s", header->concatenated.seq_number,
				//	data->payload);
			}
			count++;
			if (count == total_msgs) {
				for (int i = 1; i < (total_msgs); i++) {
					//LOG_DBG("concat_rsp_buf (%d): %s", i, concat_rsp_buf + SM_SMS_AT_HEADER_INFO_MAX_LEN + i * SMS_MAX_PAYLOAD_LEN_CHARS);
					strncat(concat_rsp_buf, concat_rsp_buf + SM_SMS_AT_HEADER_INFO_MAX_LEN + i * SMS_MAX_PAYLOAD_LEN_CHARS, SMS_MAX_PAYLOAD_LEN_CHARS);
				}
				strcat(concat_rsp_buf, "\"\r\n");
				//LOG_WRN("concat_rsp_buf: %s", concat_rsp_buf);
				rsp_send("%s", concat_rsp_buf);
			} else {
				return;
			}
done:
			ref_number = 0;
			total_msgs = 0;
			count = 0;
			if (concat_rsp_buf != NULL) {
				free(concat_rsp_buf);
				concat_rsp_buf = NULL;
			}
			last_received_uptime = 0;
		}
	} else if (data->type == SMS_TYPE_STATUS_REPORT) {
		LOG_INF("Status report received");
	} else {
		LOG_WRN("Unknown type: %d", data->type);
	}
}

static int do_sms_start(void)
{
	int err = 0;

	if (sms_handle >= 0) {
		/* already registered */
		return -EBUSY;
	}

	sms_handle = sms_register_listener(sms_callback, NULL);
	if (sms_handle < 0) {
		err = sms_handle;
		LOG_ERR("SMS start error: %d", err);
		sms_handle = -1;
	}

	return err;
}

static int do_sms_stop(void)
{
	sms_unregister_listener(sms_handle);
	sms_handle = -1;

	return 0;
}

static int do_sms_send(const char *number, const char *message, uint16_t message_len)
{
	int err;

	if (sms_handle < 0) {
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
