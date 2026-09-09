/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/tls_credentials.h>
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_cmng, CONFIG_SM_LOG_LEVEL);

/** @brief Credential type as used in the AT#XCMNG command. */
enum sm_cmng_type {
	SM_AT_CMNG_TYPE_CA_CERT     = 0,
	SM_AT_CMNG_TYPE_CLIENT_CERT = 1,
	SM_AT_CMNG_TYPE_CLIENT_KEY  = 2,
	SM_AT_CMNG_TYPE_PSK         = 3,
	SM_AT_CMNG_TYPE_PSK_ID      = 4,
	SM_AT_CMNG_TYPE_COUNT
};

/** @brief Supported opcodes for AT#XCMNG. */
enum sm_cmng_opcode {
	AT_CMNG_OP_WRITE  = 0,
	AT_CMNG_OP_LIST   = 1,
	AT_CMNG_OP_DELETE = 3
};

static enum tls_credential_type sm_cmng_to_tls_cred(enum sm_cmng_type type)
{
	switch (type) {
	case SM_AT_CMNG_TYPE_CA_CERT:     return TLS_CREDENTIAL_CA_CERTIFICATE;
	case SM_AT_CMNG_TYPE_CLIENT_CERT: return TLS_CREDENTIAL_SERVER_CERTIFICATE;
	case SM_AT_CMNG_TYPE_CLIENT_KEY:  return TLS_CREDENTIAL_PRIVATE_KEY;
	case SM_AT_CMNG_TYPE_PSK:         return TLS_CREDENTIAL_PSK;
	case SM_AT_CMNG_TYPE_PSK_ID:      return TLS_CREDENTIAL_PSK_ID;
	default:                          return TLS_CREDENTIAL_NONE;
	}
}

/* AT#XCMNG=<op>[,<sec_tag>[,<type>[,"<content>"]]]
 *
 *   op=0  Write credential.  Requires sec_tag, type, content.
 *   op=1  List credentials.  Not yet supported.
 *   op=3  Delete credential. Requires sec_tag, type.
 */
SM_AT_CMD_CUSTOM(xcmng, "AT#XCMNG", handle_at_xcmng);
static int handle_at_xcmng(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			    uint32_t param_count)
{
	uint16_t op, type;
	uint32_t sec_tag;
	int err = -EINVAL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (at_parser_num_get(parser, 1, &op)) {
			return -EINVAL;
		}

		if (op == AT_CMNG_OP_WRITE) {
			if (param_count < 5) {
				return -EINVAL;
			}
			if (at_parser_num_get(parser, 2, &sec_tag) ||
			    at_parser_num_get(parser, 3, &type) ||
			    type >= SM_AT_CMNG_TYPE_COUNT) {
				return -EINVAL;
			}

			const char *data;
			size_t len;

			if (at_parser_string_ptr_get(parser, 4, &data, &len)) {
				return -EINVAL;
			}

			/* PEM certificates and keys must be null-terminated for
			 * MbedTLS. The AT parser does not guarantee a null byte
			 * after the string, so copy and append one for PEM types.
			 * psa_ps_set() copies the data immediately, so the
			 * temporary buffer can be freed after the call.
			 */
			if (type <= SM_AT_CMNG_TYPE_CLIENT_KEY) {
				char *buf = k_malloc(len + 1);

				if (!buf) {
					return -ENOMEM;
				}
				memcpy(buf, data, len);

				/* Normalize line endings: the AT command parser drops the \n
				 * from \r\n pairs, leaving bare \r. MbedTLS PEM parser needs
				 * \n to split base64 lines.
				 * Convert \r\n -> \n and standalone \r -> \n in-place.
				 */
				size_t dst = 0;

				for (size_t src = 0; src < len; src++) {
					if (buf[src] == '\r') {
						buf[dst++] = '\n';
						if (src + 1 < len && buf[src + 1] == '\n') {
							src++;
						}
					} else {
						buf[dst++] = buf[src];
					}
				}
				len = dst;
				buf[len] = '\0';

				err = tls_credential_add(sec_tag, sm_cmng_to_tls_cred(type),
							 buf, len + 1);
				k_free(buf);
			} else {
				err = tls_credential_add(sec_tag, sm_cmng_to_tls_cred(type),
							 data, len);
			}
			if (err) {
				LOG_ERR("tls_credential_add(%u, %u) failed: %d",
					sec_tag, type, err);
			}
		} else if (op == AT_CMNG_OP_LIST) {
			/* Listing stored credentials is not yet supported with
			 * the Protected Storage backend.
			 */
			err = -ENOTSUP;
		} else if (op == AT_CMNG_OP_DELETE) {
			if (param_count < 4) {
				return -EINVAL;
			}
			if (at_parser_num_get(parser, 2, &sec_tag) ||
			    at_parser_num_get(parser, 3, &type) ||
			    type >= SM_AT_CMNG_TYPE_COUNT) {
				return -EINVAL;
			}

			err = tls_credential_delete(sec_tag, sm_cmng_to_tls_cred(type));
			if (err) {
				LOG_ERR("tls_credential_delete(%u, %u) failed: %d",
					sec_tag, type, err);
			}
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XCMNG: (0,1,3),(0-4294967295),(0-4)\r\n");
		return 0;

	default:
		break;
	}

	return err;
}
