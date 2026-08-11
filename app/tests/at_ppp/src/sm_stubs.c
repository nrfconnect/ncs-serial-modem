/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file sm_stubs.c
 *
 * Serial Modem AT host and utility stubs used by the sm_ppp.c unit tests.
 *
 * Only sm_ppp.c is compiled into this test image, so everything it uses from
 * sm_at_host.c and sm_util.c is stubbed here. All responses and URCs are
 * captured into a single buffer that the tests inspect.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>

#include "sm_at_host.h"
#include "sm_cmux.h"
#include "sm_util.h"
#include "ppp_stubs.h"

/* --- Captured output ---------------------------------------------------- */

static char output[2048];
static size_t output_len;
static char last_response[64];

/* --- AT host context model ----------------------------------------------
 *
 * sm_at_host.c keeps one context per pipe that is in AT command mode. A pipe
 * that has been handed over to another consumer (PPP, trace backend) has no
 * context, which is exactly what sm_cmux.c and sm_ppp.c rely on. The stub
 * models that with a small fixed pool.
 */

struct sm_at_host_ctx {
	struct modem_pipe *pipe;
	bool in_use;
};

static struct sm_at_host_ctx contexts[PPP_STUB_MAX_DLCI + 1];
static struct sm_at_host_ctx *current_ctx;

static struct sm_at_host_ctx *ctx_alloc(struct modem_pipe *pipe)
{
	ARRAY_FOR_EACH_PTR(contexts, ctx) {
		if (!ctx->in_use) {
			ctx->in_use = true;
			ctx->pipe = pipe;
			return ctx;
		}
	}
	return NULL;
}

static void ctx_free(struct sm_at_host_ctx *ctx)
{
	if (current_ctx == ctx) {
		current_ctx = NULL;
	}
	ctx->in_use = false;
	ctx->pipe = NULL;
}

int ppp_stub_pdn_id = 1;
bool ppp_stub_lte_enabled = true;
bool ppp_stub_registered = true;
bool ppp_stub_cid_active = true;
int ppp_stub_at_ret;
int ppp_stub_pdn_info_ret;
char ppp_stub_ipv4_addr[NET_INET_ADDRSTRLEN] = "192.0.2.1";
char ppp_stub_ipv6_addr[NET_INET6_ADDRSTRLEN];

unsigned int ppp_stub_host_release_calls;
unsigned int ppp_stub_host_attach_calls;
unsigned int ppp_stub_cmd_done_calls;

bool sm_init_failed;

static void capture(const char *fmt, va_list args)
{
	char buf[512];
	int len = vsnprintf(buf, sizeof(buf), fmt, args);

	if (len <= 0) {
		return;
	}
	if ((size_t)len >= sizeof(buf)) {
		len = sizeof(buf) - 1;
	}
	if (output_len + len >= sizeof(output)) {
		len = sizeof(output) - output_len - 1;
	}
	memcpy(output + output_len, buf, len);
	output_len += len;
	output[output_len] = '\0';
}

const char *ppp_stub_get_output(void)
{
	return output;
}

void ppp_stub_clear_output(void)
{
	memset(output, 0, sizeof(output));
	output_len = 0;
}

void ppp_stub_set_current_pipe(struct modem_pipe *pipe)
{
	if (!pipe) {
		/* No pipe is in AT command mode at all. */
		memset(contexts, 0, sizeof(contexts));
		current_ctx = NULL;
		return;
	}

	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);

	if (!ctx) {
		ctx = ctx_alloc(pipe);
	}
	current_ctx = ctx;
}

struct modem_pipe *ppp_stub_at_pipe(void)
{
	return current_ctx ? current_ctx->pipe : NULL;
}

bool ppp_stub_pipe_in_at_mode(struct modem_pipe *pipe)
{
	return sm_at_host_get_ctx_from(pipe) != NULL;
}

const char *ppp_stub_last_at_response(void)
{
	return last_response;
}

void ppp_stub_sm_reset(void)
{
	ppp_stub_clear_output();
	memset(last_response, 0, sizeof(last_response));
	ppp_stub_pdn_id = 1;
	ppp_stub_lte_enabled = true;
	ppp_stub_registered = true;
	ppp_stub_cid_active = true;
	ppp_stub_at_ret = 0;
	ppp_stub_pdn_info_ret = 0;
	strcpy(ppp_stub_ipv4_addr, "192.0.2.1");
	ppp_stub_ipv6_addr[0] = '\0';
	ppp_stub_host_release_calls = 0;
	ppp_stub_host_attach_calls = 0;
	ppp_stub_cmd_done_calls = 0;
	sm_init_failed = false;

	memset(contexts, 0, sizeof(contexts));
	current_ctx = NULL;
}

/* --- sm_at_host stubs --------------------------------------------------- */

void rsp_send(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	capture(fmt, args);
	va_end(args);
}

void rsp_send_to(struct modem_pipe *pipe, const char *fmt, ...)
{
	va_list args;

	ARG_UNUSED(pipe);
	va_start(args, fmt);
	capture(fmt, args);
	va_end(args);
}

void urc_send_to(struct modem_pipe *pipe, const char *fmt, ...)
{
	va_list args;

	ARG_UNUSED(pipe);
	va_start(args, fmt);
	capture(fmt, args);
	va_end(args);
}

void rsp_send_ok(void)
{
	rsp_send("\r\nOK\r\n");
}

void rsp_send_error(void)
{
	rsp_send("\r\nERROR\r\n");
}

void sm_at_host_cmd_done(struct sm_at_host_ctx *ctx)
{
	ARG_UNUSED(ctx);
	ppp_stub_cmd_done_calls++;
}

void sm_at_host_release(struct sm_at_host_ctx *ctx)
{
	ppp_stub_host_release_calls++;

	if (!ctx || !ctx->in_use) {
		return;
	}
	/* The pipe is handed over to another consumer and leaves AT mode. */
	ctx_free(ctx);
}

void sm_at_host_attach(struct modem_pipe *pipe)
{
	ppp_stub_host_attach_calls++;

	if (!pipe || sm_at_host_get_ctx_from(pipe)) {
		return;
	}
	/* The pipe returns to AT command mode with a fresh context. */
	(void)ctx_alloc(pipe);
}

int sm_at_host_set_pipe(struct sm_at_host_ctx *ctx, struct modem_pipe *pipe)
{
	if (!ctx || !ctx->in_use || !pipe) {
		return -EINVAL;
	}

	/* Any context already owning the target pipe is destroyed, mirroring
	 * sm_at_host_set_pipe().
	 */
	struct sm_at_host_ctx *old = sm_at_host_get_ctx_from(pipe);

	if (old && old != ctx) {
		ctx_free(old);
	}
	ctx->pipe = pipe;
	return 0;
}

struct modem_pipe *sm_at_host_get_pipe(struct sm_at_host_ctx *ctx)
{
	return (ctx && ctx->in_use) ? ctx->pipe : NULL;
}

struct sm_at_host_ctx *sm_at_host_get_ctx_from(struct modem_pipe *pipe)
{
	if (!pipe) {
		return NULL;
	}
	ARRAY_FOR_EACH_PTR(contexts, ctx) {
		if (ctx->in_use && ctx->pipe == pipe) {
			return ctx;
		}
	}
	return NULL;
}

struct sm_at_host_ctx *sm_at_host_get_current(void)
{
	return current_ctx ? current_ctx : sm_at_host_get_urc_ctx();
}

struct sm_at_host_ctx *sm_at_host_get_urc_ctx(void)
{
	if (sm_cmux_is_started()) {
		/* URCs go to the first CMUX channel that is in AT command mode. */
		for (uint8_t ch = 1; ch <= CONFIG_SM_CMUX_CHANNEL_COUNT; ch++) {
			struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(sm_cmux_get_dlci(ch));

			if (ctx) {
				return ctx;
			}
		}
	}

	/* Fall back to the oldest context, which is the one created for the
	 * UART pipe at boot.
	 */
	ARRAY_FOR_EACH_PTR(contexts, ctx) {
		if (ctx->in_use) {
			return ctx;
		}
	}
	return NULL;
}

/* --- sm_util stubs ------------------------------------------------------ */

int sm_util_at_printf(const char *fmt, ...)
{
	ARG_UNUSED(fmt);
	return ppp_stub_at_ret;
}

int sm_util_at_cmd_no_intercept(char *buf, size_t len, const char *at_cmd)
{
	ARG_UNUSED(at_cmd);

	if (ppp_stub_at_ret == 0 && buf && len) {
		snprintf(buf, len, "OK\r\n");
	}
	return ppp_stub_at_ret;
}

int util_string_get(struct at_parser *parser, size_t index, char *value, size_t *len)
{
	const size_t size = *len;
	int ret = at_parser_string_get(parser, index, value, len);

	if (ret) {
		return ret;
	}
	if (*len < size) {
		value[*len] = '\0';
		return 0;
	}
	return -ENOMEM;
}

void util_get_ip_addr(int cid, char addr4[NET_INET_ADDRSTRLEN], char addr6[NET_INET6_ADDRSTRLEN])
{
	ARG_UNUSED(cid);

	strcpy(addr4, ppp_stub_ipv4_addr);
	strcpy(addr6, ppp_stub_ipv6_addr);
}

int sm_util_pdn_id_get(uint8_t cid)
{
	ARG_UNUSED(cid);
	return ppp_stub_pdn_id;
}

int sm_util_pdn_dynamic_info_get(uint8_t cid, struct sm_pdn_dynamic_info *pdn_info)
{
	ARG_UNUSED(cid);

	if (ppp_stub_pdn_info_ret) {
		return ppp_stub_pdn_info_ret;
	}

	pdn_info->ipv4_mtu = 1464;
	pdn_info->ipv6_mtu = 0;
	return 0;
}

bool sm_util_cfun_is_lte_enabled(void)
{
	return ppp_stub_lte_enabled;
}

bool sm_util_cereg_is_registered(void)
{
	return ppp_stub_registered;
}

bool sm_util_is_cid_active(uint8_t cid)
{
	ARG_UNUSED(cid);
	return ppp_stub_cid_active;
}

/* --- nrf_modem stubs ---------------------------------------------------- */

void modem_pipe_release(struct modem_pipe *pipe)
{
	ARG_UNUSED(pipe);
}

int nrf_modem_at_notif_handler_set(nrf_modem_at_notif_handler_t callback)
{
	ARG_UNUSED(callback);
	return 0;
}

/* --- AT command dispatch ------------------------------------------------ */

int sm_at_cb_wrapper(char *buf, size_t len, char *at_cmd, sm_at_callback *cb)
{
	struct at_parser parser;
	size_t valid_count = 0;
	enum at_parser_cmd_type type;
	int err;

	err = at_parser_init(&parser, at_cmd);
	if (err) {
		return err;
	}

	err = at_parser_cmd_count_get(&parser, &valid_count);
	if (err) {
		return err;
	}

	err = at_parser_cmd_type_get(&parser, &type);
	if (err) {
		return err;
	}

	err = cb(type, &parser, valid_count);
	if (!err) {
		snprintf(buf, len, "OK\r\n");
	} else if (err > 0) {
		snprintf(buf, len, "ERROR\r\n");
	}

	return err;
}

/* Wrappers generated by SM_AT_CMD_CUSTOM() in sm_ppp.c. */
extern int handle_at_ppp_wrapper_xppp(char *buf, size_t len, char *at_cmd);
extern int handle_at_cgdata_wrapper_cgdata(char *buf, size_t len, char *at_cmd);
/* Interceptors registered with AT_CMD_CUSTOM() in sm_ppp.c. */
extern int at_cgerep_callback(char *buf, size_t len, char *at_cmd);
extern int at_cfun_set_callback(char *buf, size_t len, char *at_cmd);
/* Wrappers generated by SM_AT_CMD_CUSTOM() in sm_cmux.c. */
extern int handle_at_xcmux_wrapper_xcmux(char *buf, size_t len, char *at_cmd);
extern int handle_at_xcmuxcld_wrapper_xcmuxcld(char *buf, size_t len, char *at_cmd);
extern int handle_at_cmux_wrapper_atcmux(char *buf, size_t len, char *at_cmd);

struct at_dispatch_entry {
	const char *prefix;
	int (*handler)(char *buf, size_t len, char *at_cmd);
};

/* Longest prefix first: "AT#XCMUXCLD" must win over "AT#XCMUX". */
static const struct at_dispatch_entry at_dispatch_table[] = {
	{"AT#XCMUXCLD", handle_at_xcmuxcld_wrapper_xcmuxcld},
	{"AT#XCMUX", handle_at_xcmux_wrapper_xcmux},
	{"AT#XPPP", handle_at_ppp_wrapper_xppp},
	{"AT+CMUX", handle_at_cmux_wrapper_atcmux},
	{"AT+CGDATA", handle_at_cgdata_wrapper_cgdata},
	{"AT+CGEREP", at_cgerep_callback},
	{"AT+CFUN=", at_cfun_set_callback},
};

int ppp_stub_send_at(const char *at_cmd)
{
	char cmd[128];

	strncpy(cmd, at_cmd, sizeof(cmd) - 1);
	cmd[sizeof(cmd) - 1] = '\0';
	memset(last_response, 0, sizeof(last_response));

	ARRAY_FOR_EACH_PTR(at_dispatch_table, entry) {
		if (strncasecmp(cmd, entry->prefix, strlen(entry->prefix)) == 0) {
			return entry->handler(last_response, sizeof(last_response), cmd);
		}
	}

	return -ENOTSUP;
}
