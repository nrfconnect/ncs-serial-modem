/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <sm_at_client.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(mdm_sm_mon, CONFIG_SM_AT_CLIENT_LOG_LEVEL);

struct at_notif_fifo {
	void *fifo_reserved;
	char data[]; /* Null-terminated AT notification string */
};

static void sm_monitor_task(struct k_work *work);

static K_FIFO_DEFINE(at_monitor_fifo);
static K_HEAP_DEFINE(at_monitor_heap, 1024);
static K_WORK_DEFINE(at_monitor_work, sm_monitor_task);

extern char *strnstr(const char *haystack, const char *needle, size_t haystack_sz);

static bool is_paused(const struct sm_monitor_entry *mon)
{
	return mon->paused;
}

static bool has_match(const struct sm_monitor_entry *mon, const char *notif)
{
	return (mon->filter == MON_ANY || strstr(notif, mon->filter));
}

/* Schedules a workqueue to dispatch AT notifications. */
void sm_monitor_dispatch(const char *notif, size_t len)
{
	struct at_notif_fifo *at_notif;
	size_t sz_needed;
	size_t notif_len = 0;
	const char *current = notif;
	const char *end = notif + len;
	bool queued_any = false;

	/* TODO:
	 * - Not called if URC comes immediately after sending an AT command.
	 *   ATE1 should be used, and echoed AT-command should be matched to received data to
	 *   deduct the start of the AT response.
	 * - Cannot handle the case where URC is split over multiple UART RX buffers.
	 * - Cannot separate URC from data mode data which may also contain \r\n\ sequences.
	 */

	while (current < end) {
		/* Process each notification in the buffer.
		 * Notifications are delimited by \r\n\r\n (end of one + start of next).
		 */
		const char *next_delim = strnstr(current, "\r\n\r\n", end - current);
		bool has_monitor_match = false;

		if (next_delim) {
			/* Include the trailing \r\n in this notification */
			notif_len = (next_delim - current) + 2;
		} else {
			/* Last notification in buffer */
			notif_len = end - current;
		}

		if (notif_len == 0) {
			break;
		}

		/* Check if any monitor is interested in this notification. */
		STRUCT_SECTION_FOREACH(sm_monitor_entry, e) {
			if (!is_paused(e)) {
				if (e->filter == MON_ANY) {
					has_monitor_match = true;
					break;
				}
				if (strnstr(current, e->filter, notif_len)) {
					has_monitor_match = true;
					break;
				}
			}
		}

		if (has_monitor_match) {
			sz_needed = sizeof(struct at_notif_fifo) + notif_len + sizeof(char);
			at_notif = k_heap_alloc(&at_monitor_heap, sz_needed, K_NO_WAIT);
			if (!at_notif) {
				LOG_WRN("No heap space for incoming notification");
				/* Submit work for already queued notifications and stop. */
				break;
			}
			strncpy(at_notif->data, current, notif_len);
			at_notif->data[notif_len] = '\0';
			k_fifo_put(&at_monitor_fifo, at_notif);
			queued_any = true;
		}

		/* Move to next notification (skip the \r\n\ delimiter from the current). */
		if (next_delim) {
			current = next_delim + 2;
		} else {
			break;
		}
	}

	if (queued_any) {
		k_work_submit(&at_monitor_work);
	}
}

static void sm_monitor_task(struct k_work *work)
{
	struct at_notif_fifo *at_notif;

	while ((at_notif = k_fifo_get(&at_monitor_fifo, K_NO_WAIT))) {
		/* Match notification with all monitors */
		LOG_DBG("AT notif: %.*s", strlen(at_notif->data) - strlen("\r\n"), at_notif->data);
		STRUCT_SECTION_FOREACH(sm_monitor_entry, e) {
			if (!is_paused(e) && has_match(e, at_notif->data)) {
				LOG_DBG("Dispatching to %p", e->handler);
				e->handler(at_notif->data);
			}
		}
		k_heap_free(&at_monitor_heap, at_notif);
	}
}
