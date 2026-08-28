/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#include <zephyr/modem/chat.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/drivers/modem/modem_cellular.h>
#include <zephyr/pm/device_runtime.h>

struct at_shell_chat_instance {
	struct modem_chat at_shell_chat;
	uint8_t at_shell_chat_receive_buf[1024];
	uint8_t *at_shell_chat_argv_buf[3];
};

static const struct device *modem = DEVICE_DT_GET_ONE(nordic_nrf91_sm_v2);

static void chat_callback(struct modem_chat *chat, char **argv, uint16_t argc, void *user_data);

MODEM_CHAT_MATCH_DEFINE(ok_match, "OK", "", NULL);
/* We can only match responses that are longer than 3 characters, so it does not
 * eat "OK". All others need to be handled in chat_callback().
 */
MODEM_CHAT_MATCH_WILDCARD_DEFINE(all_match, "???", "", chat_callback);

/* Helper copied from Serial Modem's sm_util.h */
#define Z_MODEM_PIPE_EVENT_OPENED_BIT BIT(0)

static inline bool sm_pipe_is_open(struct modem_pipe *pipe)
{
	return k_event_test(&pipe->event, Z_MODEM_PIPE_EVENT_OPENED_BIT) ==
	       Z_MODEM_PIPE_EVENT_OPENED_BIT;
}

static struct modem_chat *at_shell_init_chat(const struct shell *sh)
{
	struct at_shell_chat_instance *instance =
		(struct at_shell_chat_instance *)malloc(sizeof(struct at_shell_chat_instance));

	if (instance == NULL) {
		shell_error(sh, "Failed to allocate chat buffers");
		return NULL;
	}

	const struct modem_chat_config at_shell_chat_config = {
		.receive_buf = instance->at_shell_chat_receive_buf,
		.receive_buf_size = sizeof(instance->at_shell_chat_receive_buf),
		.delimiter = "\r",
		.delimiter_size = sizeof("\r") - 1,
		.filter = "\n",
		.filter_size = sizeof("\n") - 1,
		.argv = instance->at_shell_chat_argv_buf,
		.argv_size = ARRAY_SIZE(instance->at_shell_chat_argv_buf),
		.unsol_matches = &all_match,
		.unsol_matches_size = 1,
		.user_data = (void *)sh,
	};

	modem_chat_init(&instance->at_shell_chat, &at_shell_chat_config);
	return &instance->at_shell_chat;
}

static void at_shell_release_chat(struct modem_chat *chat)
{
	modem_chat_release(chat);
	free(chat);
}

static void chat_callback(struct modem_chat *chat, char **argv, uint16_t argc, void *user_data)
{
	const struct shell *sh = (const struct shell *)user_data;
	char *s;
	size_t len = 0;

	/* Because we use "???" wildcard match, we need to concatenate all arguments so we can
	 * properly match reponses because the parameteres for "ERROR" would like:
	 * argv[0] = "ERR"
	 * argv[1] = "OR"
	 */
	for (uint16_t i = 0; i < argc; i++) {
		len += strlen(argv[i]);
	}

	s = (char *)calloc(1, len + 1);
	if (s == NULL) {
		shell_error(sh, "out of mem");
		return;
	}
	for (uint16_t i = 0; i < argc; i++) {
		strcat(s, argv[i]);
}

	shell_print(sh, "%s", s);

	/* We need to handle typical error matches here */
	if (strstr(s, "ERROR") != NULL ||
	    strstr(s, "BUSY") != NULL ||
	    strstr(s, "NO CARRIER") != NULL) {
		modem_chat_script_abort(chat);
	}
	free(s);
}

static int chat_cmd(struct modem_chat *chat, const char *fmt, ...)
{
	va_list args;
	uint8_t buf[256];

	va_start(args, fmt);
	vsnprintf((char *)buf, sizeof(buf), fmt, args);
	va_end(args);

	struct modem_chat_script_chat cmd = {
		.request = buf,
		.request_size = strlen((char *)buf),
		.response_matches = &ok_match,
		.response_matches_size = 1,
		.timeout = 1000,
	};

	struct modem_chat_script script = {
		.name = "chat_cmd",
		.script_chats = &cmd,
		.script_chats_size = 1,
		.callback = NULL,
		.timeout = 10,
	};

	return modem_chat_run_script(chat, &script);
}

static struct modem_pipe *at_shell_get_user_pipe(const struct shell *sh)
{
	struct modem_cellular_config *config = (struct modem_cellular_config *)modem->config;

	if (config->user_pipes_size == 0) {
		shell_error(sh, "No user pipes configured");
		return NULL;
	}
	for (int i = 0; i < config->user_pipes_size; i++) {
		struct modem_cellular_user_pipe *user_pipe = &config->user_pipes[i];
		struct modem_pipelink *pipelink = user_pipe->pipelink;

		/* Find a user pipe that has not been attached */
		if (pipelink->callback == NULL) {
			return user_pipe->pipe;
		}
	}

	shell_error(sh, "Cannot find user pipe");
	return NULL;
}

/*
 * Return any modem pipe that can be used to send AT commands to the modem.
 * When modem is not active, return the modem's UART pipe.
 * When modem is active, return first user pipe that is available, or NULL if none are available.
 */
static struct modem_pipe *at_shell_get_pipe(const struct shell *sh)
{
	struct modem_cellular_config *config = (struct modem_cellular_config *)modem->config;
	struct modem_cellular_data *data = (struct modem_cellular_data *)modem->data;

	/* Ensure that modem is not used */
	int cnt = pm_device_runtime_usage(modem);

	if (cnt > 0) {
		return at_shell_get_user_pipe(sh);
	}

	/* Ensure that UART is free */
	if (!device_is_ready(config->uart)) {
		shell_error(sh, "UART is not ready");
		return NULL;
	}

	cnt = pm_device_runtime_usage(config->uart);
	if (cnt > 0) {
		shell_error(sh, "UART is in use, cannot run AT command");
		return NULL;
	}

	return data->uart_pipe;
}

static int cmd_at(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	if (argc < 2) {
		shell_error(sh, "Usage: at <command>");
		return -EINVAL;
	}

	if (!device_is_ready(modem)) {
		shell_error(sh, "Modem is not ready");
		return -ENODEV;
	}

	struct modem_pipe *pipe = at_shell_get_pipe(sh);

		if (pipe == NULL) {
			return -ENODEV;
		}

	bool pipe_was_open = sm_pipe_is_open(pipe);

	modem_pipe_release(pipe);
	ret = modem_pipe_open(pipe, K_SECONDS(1));
	if (ret < 0) {
		shell_error(sh, "Failed to open UART pipe: %d", ret);
		return ret;
	}

	struct modem_chat *chat = at_shell_init_chat(sh);

	if (chat == NULL) {
		ret = -ENOMEM;
		goto close_pipe;
		}

	modem_chat_attach(chat, pipe);

	ret = chat_cmd(chat, "%s", argv[1]);
	if (ret < 0) {
		shell_error(sh, "Failed to run AT command: %d", ret);
	} else {
		shell_print(sh, "OK");
	}

	at_shell_release_chat(chat);

close_pipe:
	if (!pipe_was_open) {
		modem_pipe_close(pipe, K_SECONDS(1));
	}
	return ret;
}
SHELL_CMD_ARG_REGISTER(at, NULL, "AT command\nat <AT command>", cmd_at, 2, 0);

#ifndef CONFIG_MODEM_AT_SHELL

/* If modem AT shell is not enabled, provide a wrapper for the "modem at" */
static int cmd_modem_at(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3 || strcmp(argv[1], "at") != 0) {
		shell_error(sh, "Usage: modem at <command>");
		return -EINVAL;
	}
	return cmd_at(sh, argc - 1, &argv[1]);
}
SHELL_CMD_ARG_REGISTER(modem, NULL, "modem at <AT command>", cmd_modem_at, 3, 0);

#endif
