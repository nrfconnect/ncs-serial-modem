/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/random/random.h>
#include <zephyr/net/socket.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_icmp, CONFIG_SM_LOG_LEVEL);

#define ICMP_DEFAULT_LINK_MTU    1500
#define ICMP_HDR_LEN             8

/* Maximum payload that still fits the ICMP_DEFAULT_LINK_MTU sized packet buffer allocated in
 * send_ping_wait_reply(). The IPv6 limit is lower because its header is larger.
 */
#define ICMP_IPV4_MAX_LEN        (ICMP_DEFAULT_LINK_MTU - NET_IPV4H_LEN - ICMP_HDR_LEN)
#define ICMP_IPV6_MAX_LEN        (ICMP_DEFAULT_LINK_MTU - NET_IPV6H_LEN - ICMP_HDR_LEN)

/* Next header: */
#define IP_NEXT_HEADER_POS  6
#define IP_PROTOCOL_POS     9

#define ICMP_ECHO_REP       0
#define ICMP_ECHO_REQ       8
#define ICMP6_ECHO_REQ      128
#define ICMP6_ECHO_REP      129

/**@ ICMP Ping command arguments */
static struct ping_argv_t {
	struct zsock_addrinfo *src;
	struct zsock_addrinfo *dest;
	int family;
	uint16_t len;
	uint32_t waitms;
	uint16_t count;
	uint32_t interval;
	uint16_t pdn;
	uint16_t id;
	uint16_t seq;
} ping_argv;
static struct modem_pipe *ping_pipe;
static atomic_t ping_in_progress;

static void ping_task(struct k_work *item);
K_WORK_DEFINE(ping_work, ping_task);

static inline void setip(uint8_t *buffer, uint32_t ipaddr)
{
	buffer[0] = ipaddr & 0xFF;
	buffer[1] = (ipaddr >> 8) & 0xFF;
	buffer[2] = (ipaddr >> 16) & 0xFF;
	buffer[3] = ipaddr >> 24;
}

static uint16_t check_ics(const uint8_t *buffer, int len)
{
	const uint32_t *ptr32 = (const uint32_t *)buffer;
	uint32_t hcs = 0;
	const uint16_t *ptr16;

	for (int i = len / 4; i > 0; i--) {
		uint32_t s = *ptr32++;

		hcs += s;
		if (hcs < s) {
			hcs++;
		}
	}

	ptr16 = (const uint16_t *)ptr32;

	if (len & 2) {
		uint16_t s = *ptr16++;

		hcs += s;
		if (hcs < s) {
			hcs++;
		}
	}

	if (len & 1) {
		const uint8_t *ptr8 = (const uint8_t *)ptr16;
		uint8_t s = *ptr8;

		hcs += s;
		if (hcs < s) {
			hcs++;
		}
	}

	while (hcs > 0xFFFF) {
		hcs = (hcs & 0xFFFF) + (hcs >> 16);
	}

	return ~hcs;    /* One's complement */
}

static void calc_ics(uint8_t *buffer, int len, int hcs_pos)
{
	uint16_t *ptr_hcs = (uint16_t *)(buffer + hcs_pos);
	*ptr_hcs = 0;   /* Clear checksum before calculation */
	uint16_t hcs;

	hcs = check_ics(buffer, len);
	*ptr_hcs = hcs;
}

/**
 * @brief Compute the ICMPv6 checksum, including the IPv6 pseudo header.
 *
 * See RFC 4443 section 2.3 and RFC 2460 section 8.1. The checksum field itself is skipped.
 *
 * @param ip       IPv6 header.
 * @param icmp     ICMPv6 message.
 * @param icmp_len Length of the ICMPv6 message, at least ICMP_HDR_LEN.
 */
static uint16_t icmp6_checksum(const uint8_t *ip, const uint8_t *icmp, int icmp_len)
{
	uint8_t tbuf[2] __aligned(2) = {0, ip[IP_NEXT_HEADER_POS]};
	uint32_t hcs;

	hcs = check_ics(ip + 8, 32);            /* Pseudo header: source + dest */
	hcs += check_ics(ip + 4, 2);            /* Pseudo header: payload length */
	hcs += check_ics(tbuf, 2);              /* Pseudo header: next header */
	hcs += check_ics(icmp, 2);              /* ICMP: type & code */
	hcs += check_ics(icmp + 4, icmp_len - 4); /* ICMP: header data + data */

	while (hcs > 0xFFFF) {
		hcs = (hcs & 0xFFFF) + (hcs >> 16);
	}

	return hcs;
}

/**
 * @brief Check whether a received packet is the echo reply to our request.
 *
 * Everything that arrives on the PDN is delivered to the raw socket, so the packet is
 * untrusted. It must be a well formed echo reply, from the address that was pinged, carrying
 * the identifier and the sequence number of the request, with valid checksum and length.
 * Anything else is not a reply to us and must simply be ignored.
 *
 * @param buf Received packet, starting from the IP header.
 * @param len Length of the received packet.
 * @param seq Sequence number of the request.
 */
static bool is_echo_reply(const uint8_t *buf, int len, uint16_t seq)
{
	const uint8_t *icmp;
	int header_len;
	int ip_len;

	if (ping_argv.family == NET_AF_INET) {
		const struct sockaddr_in *dst = (const struct sockaddr_in *)ping_argv.dest->ai_addr;

		if (len < NET_IPV4H_LEN || (buf[0] >> 4) != 4) {
			return false;
		}
		/* The reply may carry IP options */
		header_len = (buf[0] & 0x0F) * 4;
		if (header_len < NET_IPV4H_LEN || len < header_len + ICMP_HDR_LEN) {
			return false;
		}
		if (buf[IP_PROTOCOL_POS] != IPPROTO_ICMP) {
			return false;
		}
		/* Source address */
		if (memcmp(buf + 12, &dst->sin_addr.s_addr, 4) != 0) {
			return false;
		}
		ip_len = (buf[2] << 8) + buf[3];
		if (ip_len != len) {
			return false;
		}

		icmp = buf + header_len;
		if (icmp[0] != ICMP_ECHO_REP || icmp[1] != 0) {
			return false;
		}
		if (check_ics(icmp, len - header_len) != 0) {
			return false;
		}
	} else {
		const struct sockaddr_in6 *dst =
			(const struct sockaddr_in6 *)ping_argv.dest->ai_addr;
		uint16_t plhcs;

		header_len = NET_IPV6H_LEN;
		if (len < header_len + ICMP_HDR_LEN || (buf[0] >> 4) != 6) {
			return false;
		}
		if (buf[IP_NEXT_HEADER_POS] != IPPROTO_ICMPV6) {
			return false;
		}
		/* Source address */
		if (memcmp(buf + 8, dst->sin6_addr.s6_addr, 16) != 0) {
			return false;
		}
		ip_len = (buf[4] << 8) + buf[5] + header_len;
		if (ip_len != len) {
			return false;
		}

		icmp = buf + header_len;
		if (icmp[0] != ICMP6_ECHO_REP || icmp[1] != 0) {
			return false;
		}
		plhcs = icmp[2] + (icmp[3] << 8);
		if (plhcs != icmp6_checksum(buf, icmp, len - header_len)) {
			return false;
		}
	}

	if (((icmp[4] << 8) + icmp[5]) != ping_argv.id || ((icmp[6] << 8) + icmp[7]) != seq) {
		return false;
	}

	return true;
}

/**
 * @brief Send one ICMP echo request and wait for the reply.
 *
 * The wait ends at a fixed deadline measured from the moment the request is sent. Packets
 * that are not the reply do not extend it.
 *
 * @param[in]  seq     Sequence number of the request.
 * @param[out] rtt_ms  Round-trip time in milliseconds, set only on success.
 *
 * @retval 0 A valid reply was received.
 * @retval -ETIMEDOUT No valid reply was received in time.
 * @retval -ENOMEM Failed to allocate the packet buffer.
 * @retval other Negative errno of the failed socket operation.
 */
static int send_ping_wait_reply(uint16_t seq, uint32_t *rtt_ms)
{
	int64_t start_t;
	int64_t remaining;
	uint16_t total_length;
	uint8_t *buf = NULL;
	uint8_t *data = NULL;
	uint8_t header_len = 0;
	const int alloc_size = ICMP_DEFAULT_LINK_MTU;
	struct zsock_pollfd fds[1];
	int len;
	int fd;
	int ret;
	int err = 0;
	const uint16_t icmp_hdr_len = ICMP_HDR_LEN;
	struct timeval tv;

	if (ping_argv.family == NET_AF_INET) {
		/* Generate IPv4 ICMP EchoReq */

		/* Ping header */
		header_len = NET_IPV4H_LEN;
		total_length = ping_argv.len + header_len + icmp_hdr_len;
		buf = calloc(1, alloc_size);
		if (buf == NULL) {
			LOG_ERR("Ping alloc failed: %d B", alloc_size);
			return -ENOMEM;
		}

		buf[0] = (4 << 4) + (header_len / 4);      /* Version & header length */
		/* buf[1] = 0; */                          /* Type of service */
		buf[2] = total_length >> 8;                /* Total length */
		buf[3] = total_length & 0xFF;              /* Total length */
		/* buf[4..5] = 0; */                       /* Identification */
		/* buf[6..7] = 0; */                       /* Flags & fragment offset */
		buf[8] = 64;                               /* TTL */
		buf[9] = IPPROTO_ICMP;                     /* Protocol */
		/* buf[10..11] */                          /* ICS, calculated later */

		struct sockaddr_in *sa = (struct sockaddr_in *)ping_argv.src->ai_addr;

		setip(buf + 12, sa->sin_addr.s_addr);      /* Source */
		sa = (struct sockaddr_in *)ping_argv.dest->ai_addr;
		setip(buf + 16, sa->sin_addr.s_addr);      /* Destination */

		calc_ics(buf, header_len, 10);

		/* ICMP header */
		data = buf + header_len;
		data[0] = ICMP_ECHO_REQ;                    /* Type (echo req) */
		/* data[1] = 0; */                          /* Code */
		/* data[2..3] */                            /* checksum, calculated later */
		data[4] = ping_argv.id >> 8;                /* Identifier */
		data[5] = ping_argv.id & 0xFF;              /* Identifier */
		data[6] = seq >> 8;                         /* Sequence number */
		data[7] = seq & 0xFF;                       /* Sequence number */

		/* Payload */
		for (int i = 8; i < total_length - header_len; i++) {
			data[i] = (i + seq) % 10 + '0';
		}

		/* ICMP CRC */
		calc_ics(data, total_length - header_len, 2);
	} else {
		/* Generate IPv6 ICMP EchoReq */

		/* ipv6 header */
		header_len = NET_IPV6H_LEN;

		uint16_t payload_length = ping_argv.len + icmp_hdr_len;

		total_length = payload_length + header_len;
		buf = calloc(1, alloc_size);
		if (buf == NULL) {
			LOG_ERR("Ping alloc failed: %d B", alloc_size);
			return -ENOMEM;
		}

		buf[0] = (6 << 4);              /* Version & traffic class 4 bits */
		/* buf[1..3] = 0; */            /* Traffic class 4 bits & flow label */
		buf[4] = payload_length >> 8;   /* Payload length */
		buf[5] = payload_length & 0xFF; /* Payload length */
		buf[6] = IPPROTO_ICMPV6;        /* Next header (58) */
		buf[7] = 64;                    /* Hop limit */

		struct sockaddr_in6 *sa = (struct sockaddr_in6 *)ping_argv.src->ai_addr;

		memcpy(buf + 8, sa->sin6_addr.s6_addr, 16); /* Source address */
		sa = (struct sockaddr_in6 *)ping_argv.dest->ai_addr;
		memcpy(buf + 24, sa->sin6_addr.s6_addr, 16); /* Destination address */

		/* ICMPv6 header */
		data = buf + header_len;
		data[0] = ICMP6_ECHO_REQ;        /* Type (echo req) */
		/* data[1] = 0; */               /* Code */
		/* data[2..3] */                 /* checksum, calculated later */
		data[4] = ping_argv.id >> 8;     /* Identifier */
		data[5] = ping_argv.id & 0xFF;   /* Identifier */
		data[6] = seq >> 8;              /* Sequence number */
		data[7] = seq & 0xFF;            /* Sequence number */

		/* Payload */
		for (int i = 0; i < ping_argv.len; i++) {
			data[i + icmp_hdr_len] = (i + seq) % 10 + '0';
		}

		/* ICMPv6 CRC */
		uint16_t hcs = icmp6_checksum(buf, data, icmp_hdr_len + ping_argv.len);

		data[2] = hcs & 0xFF;
		data[3] = hcs >> 8;
	}

	/* Send the ping */
	errno = 0;

	fd = zsock_socket(AF_PACKET, SOCK_RAW, 0);
	if (fd < 0) {
		err = -errno;
		LOG_ERR("zsock_socket() error: %d", err);
		free(buf);
		return err;
	}

	/* Use non-primary PDN if specified, fail if cannot proceed
	 */
	int pdn = 0;

	if (ping_argv.pdn != 0) {
		pdn = ping_argv.pdn;

		if (zsock_setsockopt(fd, SOL_SOCKET, SO_BINDTOPDN, &pdn, sizeof(int))) {
			err = -errno;
			LOG_WRN("SO_BINDTOPDN error: %d", err);
			goto close_end;
		}
	}
	LOG_DBG("Use PDN: %d", pdn);

	/* We have a blocking socket and we do not want to block for
	 * a long for sending. Thus, let's set the timeout:
	 */
	tv.tv_sec = (ping_argv.waitms / 1000);
	tv.tv_usec = (ping_argv.waitms % 1000) * 1000;

	if (zsock_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *)&tv,
		       sizeof(struct timeval)) < 0) {
		LOG_WRN("SO_SNDTIMEO error: %d", -errno);
	}

	/* Just for sure, let's put the timeout for rcv as well
	 * (should not be needed for non-blocking socket):
	 */
	if (zsock_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv,
		       sizeof(struct timeval)) < 0) {
		LOG_WRN("SO_RCVTIMEO error: %d", -errno);
	}

	/* Include also a sending time to measured RTT: */
	start_t = k_uptime_get();

	ret = zsock_send(fd, buf, total_length, 0);
	if (ret <= 0) {
		err = errno ? -errno : -EIO;
		LOG_ERR("zsock_send() error: %d", err);
		goto close_end;
	}

	fds[0].fd = fd;
	fds[0].events = ZSOCK_POLLIN;

	while (true) {
		remaining = (int64_t)ping_argv.waitms - (k_uptime_get() - start_t);
		if (remaining <= 0) {
			LOG_WRN("Ping timeout");
			err = -ETIMEDOUT;
			goto close_end;
		}

		ret = zsock_poll(fds, 1, MIN(remaining, (int64_t)INT_MAX));
		if (ret == 0) {
			/* Re-evaluate the deadline above */
			continue;
		} else if (ret < 0) {
			err = -errno;
			LOG_ERR("zsock_poll() error: %d", err);
			goto close_end;
		}

		len = zsock_recv(fd, buf, alloc_size, 0);
		if (len < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				err = -errno;
				LOG_ERR("zsock_recv() error: %d", err);
				goto close_end;
			}
			continue;
		}

		if (is_echo_reply(buf, len, seq)) {
			*rtt_ms = (uint32_t)(k_uptime_get() - start_t);
			err = 0;
			goto close_end;
		}

		/* Not the reply to our request, keep waiting until the deadline */
		LOG_DBG_RATELIMIT_RATE(1000, "Ignoring packet: %d B", len);
	}

close_end:
	(void)zsock_close(fd);
	free(buf);
	return err;
}

static void ping_task(struct k_work *item)
{
	struct zsock_addrinfo *si = ping_argv.src;
	struct zsock_addrinfo *di = ping_argv.dest;
	uint64_t sum = 0;
	uint32_t count = 0;
	uint32_t rtt_min = 0xFFFFFFFF;
	uint32_t rtt_max = 0;
	uint32_t lost;

	ARG_UNUSED(item);

	for (int i = 0; i < ping_argv.count; i++) {
		uint32_t rtt = 0;

		if (send_ping_wait_reply((uint16_t)(ping_argv.seq + i), &rtt) == 0) {
			count++;
			sum += rtt;
			rtt_max = MAX(rtt_max, rtt);
			rtt_min = MIN(rtt_min, rtt);
			urc_send_to(ping_pipe, "#XPING: %u.%03u seconds\r\n", rtt / 1000,
				    rtt % 1000);
		}
		if (i + 1 < ping_argv.count) {
			k_sleep(K_MSEC(ping_argv.interval));
		}
	}

	lost = ping_argv.count - count;
	LOG_INF("Packets sent=%u received=%u lost=%u (%u%% loss)",
		ping_argv.count, count, lost, lost * 100 / ping_argv.count);

	if (count > 1) {
		/* The average of 32-bit values fits in 32 bits */
		uint32_t avg = (uint32_t)((sum + count / 2) / count);

		urc_send_to(ping_pipe, "#XPING: average %u.%03u seconds\r\n",
			    avg / 1000, avg % 1000);

		LOG_INF("RTT min=%ums max=%ums avg=%ums", rtt_min, rtt_max, avg);
	}

	zsock_freeaddrinfo(si);
	zsock_freeaddrinfo(di);

	atomic_clear(&ping_in_progress);
}

static int ping_test_handler(const char *target, struct ping_argv_t *args)
{
	int ret;
	struct zsock_addrinfo *res;

	ret = zsock_getaddrinfo(target, NULL, NULL, &res);
	if (ret != 0) {
		LOG_ERR("zsock_getaddrinfo(dest) error: %d", ret);
		return -EAGAIN;
	}

	/* Use the first result to decide which address family to use */
	if (res->ai_family == NET_AF_INET) {
		char ipv4_addr[NET_INET_ADDRSTRLEN];

		args->family = NET_AF_INET;
		if (args->len > ICMP_IPV4_MAX_LEN) {
			LOG_ERR("Ping length too long for %s: len=%u max=%d", "IPv4",
				args->len, ICMP_IPV4_MAX_LEN);
			zsock_freeaddrinfo(res);
			return -EINVAL;
		}

		LOG_INF("Ping target's IPv4 address");
		util_get_ip_addr(args->pdn, ipv4_addr, NULL);
		if (!*ipv4_addr) {
			LOG_ERR("Local IPv4 address not found");
			zsock_freeaddrinfo(res);
			return -1;
		}

		args->dest = res;
		res = NULL;
		ret = zsock_getaddrinfo(ipv4_addr, NULL, NULL, &res);
		if (ret != 0) {
			LOG_ERR("zsock_getaddrinfo(src) error: %d", ret);
			zsock_freeaddrinfo(args->dest);
			args->dest = NULL;
			return -EAGAIN;
		}
		args->src = res;
	} else if (res->ai_family == NET_AF_INET6) {
		char ipv6_addr[NET_INET6_ADDRSTRLEN];

		args->family = NET_AF_INET6;
		if (args->len > ICMP_IPV6_MAX_LEN) {
			LOG_ERR("Ping length too long for %s: len=%u max=%d", "IPv6",
				args->len, ICMP_IPV6_MAX_LEN);
			zsock_freeaddrinfo(res);
			return -EINVAL;
		}

		LOG_INF("Ping target's IPv6 address");
		util_get_ip_addr(args->pdn, NULL, ipv6_addr);
		if (!*ipv6_addr) {
			LOG_ERR("Local IPv6 address not found");
			zsock_freeaddrinfo(res);
			return -1;
		}

		args->dest = res;
		res = NULL;
		ret = zsock_getaddrinfo(ipv6_addr, NULL, NULL, &res);
		if (ret != 0) {
			LOG_ERR("zsock_getaddrinfo(src) error: %d", ret);
			zsock_freeaddrinfo(args->dest);
			args->dest = NULL;
			return -EAGAIN;
		}
		args->src = res;
	} else {
		LOG_ERR("Address family not supported: %d", res->ai_family);
		zsock_freeaddrinfo(res);
		return -EAFNOSUPPORT;
	}

	return 0;
}

SM_AT_CMD_CUSTOM(xping, "AT#XPING", handle_at_icmp_ping);
STATIC int handle_at_icmp_ping(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	int err = -EINVAL;
	char target[SM_MAX_DNS_LEN + 1] = {0};
	size_t size = sizeof(target);
	/* Parsed into a local copy so that a command rejected below cannot alter the request
	 * that an already queued ping_task() reads.
	 */
	struct ping_argv_t args = {0};

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = util_string_get(parser, 1, target, &size);
		if (err < 0) {
			return err;
		}
		err = at_parser_num_get(parser, 2, &args.len);
		if (err < 0) {
			return err;
		}
		err = at_parser_num_get(parser, 3, &args.waitms);
		if (err < 0) {
			return err;
		}
		args.count = 1; /* default 1 */
		if (param_count > 4) {
			err = at_parser_num_get(parser, 4, &args.count);
			if (err < 0) {
				return err;
			};
			if (args.count == 0) {
				LOG_ERR("Count must be greater than 0");
				return -EINVAL;
			}
		}
		args.interval = 1000; /* default 1s */
		if (param_count > 5) {
			err = at_parser_num_get(parser, 5, &args.interval);
			if (err < 0) {
				return err;
			};
		}
		args.pdn = 0; /* default 0 primary PDN */
		if (param_count > 6) {
			err = at_parser_num_get(parser, 6, &args.pdn);
			if (err < 0) {
				return err;
			};
		}

		if (!atomic_cas(&ping_in_progress, 0, 1)) {
			return -EBUSY;
		}

		err = ping_test_handler(target, &args);
		if (err) {
			atomic_clear(&ping_in_progress);
			return err;
		}

		uint32_t rnd = sys_rand32_get();

		/* Hand the validated request over to ping_task(), which releases it. */
		args.id = rnd & 0xFFFF;
		args.seq = rnd >> 16;
		ping_argv = args;
		ping_pipe = sm_at_host_get_current_pipe();
		err = k_work_submit_to_queue(&sm_work_q, &ping_work);
		if (err < 0) {
			LOG_ERR("Failed to schedule ping: %d", err);
			zsock_freeaddrinfo(ping_argv.src);
			zsock_freeaddrinfo(ping_argv.dest);
			atomic_clear(&ping_in_progress);
			return err;
		}
		err = 0;
		break;

	default:
		break;
	}

	return err;
}
