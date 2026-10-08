/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file test_at_icmp.c
 * Unit tests for sm_at_icmp.c (AT#XPING)
 *
 * The raw packet socket is replaced by a small network emulator: zsock_send() captures the echo
 * request and, according to a per request script, queues replies for zsock_recv(). The emulator
 * can also deliver unrelated traffic, and it runs in the virtual time of native_sim, so that
 * timeout behavior can be tested without waiting.
 *
 * The checksums of the requests are verified, and the replies are built, with an implementation
 * of RFC 1071 that is independent of the one in sm_at_icmp.c.
 */

#include "unity.h"
#include <zephyr/kernel.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sm_at_host.h"
#include "uart_stub.h"

/* CMock-generated mocks */
#include "cmock_nrf_modem_at.h"
#include "cmock_nrf_socket.h"
#include "zephyr/net/cmock_socket.h"

/* Response capture - provided by sm_at_host_stubs.c */
extern const char *get_captured_response(void);
extern void clear_captured_response(void);

/* Response to AT+CGPADDR - provided by nrf_modem_at_wrapper.c */
extern const char *test_cgpaddr_response;

#define GAI_FAIL (-3)

#define PKT_MAX     1600
#define QUEUE_LEN   16
#define MAX_REQ     4
#define MAX_STEPS   4
#define IPV4_HDR    20
#define IPV6_HDR    40
#define ICMP_HDR    8

#define ICMP_ECHO_REQ   8
#define ICMP_ECHO_REP   0
#define ICMP6_ECHO_REQ  128
#define ICMP6_ECHO_REP  129

/* What is wrong with a scripted reply. Everything else in the reply is valid. */
#define F_NONE        0
#define F_BAD_CSUM    BIT(0)
#define F_TYPE_REQ    BIT(1) /* An echo request instead of a reply */
#define F_WRONG_ID    BIT(2)
#define F_WRONG_SEQ   BIT(3)
#define F_WRONG_SRC   BIT(4)
#define F_IP_OPTIONS  BIT(5) /* Valid, but the IPv4 header carries options */

static const uint8_t v4_dest[4] = {1, 2, 3, 4};
static const uint8_t v4_local[4] = {10, 0, 0, 2};
static const uint8_t v6_dest[16] = {0x20, 0x01, 0x0d, 0xb8, [15] = 1};
static const uint8_t v6_local[16] = {0x20, 0x01, 0x0d, 0xb8, [15] = 2};

/* Network emulator state */
struct pkt {
	uint8_t data[PKT_MAX];
	int len;
	int32_t delay_ms; /* Time from the previous event until this packet arrives */
};

struct step {
	unsigned int flags;
	int32_t delay_ms;
};

static struct pkt queue[QUEUE_LEN];
static int q_head;
static int q_tail;
static int32_t junk_gap_ms;
static struct step script[MAX_REQ][MAX_STEPS];
static int script_len[MAX_REQ];

static uint8_t last_req[PKT_MAX];
static int last_req_len;
static int req_count;
static uint16_t req_id[MAX_REQ];
static uint16_t req_seq[MAX_REQ];
static int socket_calls;
static int poll_timeout_min;

/* Address resolution state */
struct ai_holder {
	struct zsock_addrinfo ai;
	union {
		struct net_sockaddr_in in;
		struct net_sockaddr_in6 in6;
	} addr;
};
static struct ai_holder dest_ai;
static struct ai_holder local_ai;
static const char *local_str;
static int gai_dest_ret;
static int gai_src_ret;

static K_SEM_DEFINE(freed_sem, 0, 16);

/* Independent RFC 1071 implementation, on big endian words */
static uint16_t inet_cs(const uint8_t *d, int n)
{
	uint32_t s = 0;

	for (int i = 0; i + 1 < n; i += 2) {
		s += (d[i] << 8) | d[i + 1];
	}
	if (n & 1) {
		s += d[n - 1] << 8;
	}
	while (s >> 16) {
		s = (s & 0xFFFF) + (s >> 16);
	}
	return ~s & 0xFFFF;
}

/* Checksum of an ICMPv6 message including the pseudo header (RFC 8200 section 8.1). Returns 0 for
 * a message with a correct checksum, and the checksum to store for one with a zeroed checksum.
 */
static uint16_t icmp6_cs(const uint8_t *ip, const uint8_t *icmp, int n)
{
	static uint8_t tmp[IPV6_HDR + PKT_MAX];

	memcpy(tmp, ip + 8, 32);
	memset(tmp + 32, 0, 8);
	tmp[34] = n >> 8;
	tmp[35] = n & 0xFF;
	tmp[39] = 58;
	memcpy(tmp + 40, icmp, n);
	return inet_cs(tmp, 40 + n);
}

static void fill_ai(struct ai_holder *h, bool v6, const uint8_t *addr)
{
	memset(h, 0, sizeof(*h));
	h->ai.ai_family = v6 ? NET_AF_INET6 : NET_AF_INET;
	h->ai.ai_addr = (struct net_sockaddr *)&h->addr;
	if (v6) {
		h->addr.in6.sin6_family = NET_AF_INET6;
		memcpy(&h->addr.in6.sin6_addr, addr, 16);
		h->ai.ai_addrlen = sizeof(h->addr.in6);
	} else {
		h->addr.in.sin_family = NET_AF_INET;
		memcpy(&h->addr.in.sin_addr, addr, 4);
		h->ai.ai_addrlen = sizeof(h->addr.in);
	}
}

static void use_v4(void)
{
	fill_ai(&dest_ai, false, v4_dest);
	fill_ai(&local_ai, false, v4_local);
	local_str = "10.0.0.2";
	test_cgpaddr_response = "+CGPADDR: 0,\"10.0.0.2\"\r\nOK\r\n";
}

static void use_v6(void)
{
	fill_ai(&dest_ai, true, v6_dest);
	fill_ai(&local_ai, true, v6_local);
	local_str = "2001:DB8::2";
	test_cgpaddr_response = "+CGPADDR: 0,\"2001:DB8::2\"\r\nOK\r\n";
}

static void script_add(int req, unsigned int flags, int32_t delay_ms)
{
	TEST_ASSERT_LESS_THAN(MAX_REQ, req);
	TEST_ASSERT_LESS_THAN(MAX_STEPS, script_len[req]);
	script[req][script_len[req]].flags = flags;
	script[req][script_len[req]].delay_ms = delay_ms;
	script_len[req]++;
}

/* Build the reply to @p req, with the problem described by @p flags. */
static void build_reply(const uint8_t *req, int len, unsigned int flags, struct pkt *p)
{
	bool v6 = (req[0] >> 4) == 6;
	int ip_hdr = v6 ? IPV6_HDR : IPV4_HDR;
	int opt = (!v6 && (flags & F_IP_OPTIONS)) ? 4 : 0;
	int icmp_len = len - ip_hdr;
	uint8_t *out = p->data;
	uint8_t *icmp = out + ip_hdr + opt;
	uint8_t tmp[16];
	uint16_t cs;

	memset(p, 0, sizeof(*p));
	memcpy(out, req, ip_hdr);
	memcpy(icmp, req + ip_hdr, icmp_len);
	p->len = len + opt;

	/* The reply comes from the destination of the request */
	if (v6) {
		memcpy(tmp, out + 8, 16);
		memcpy(out + 8, out + 24, 16);
		memcpy(out + 24, tmp, 16);
	} else {
		memcpy(tmp, out + 12, 4);
		memcpy(out + 12, out + 16, 4);
		memcpy(out + 16, tmp, 4);
		if (opt) {
			out[0] = 0x46;
			memset(out + IPV4_HDR, 0x01, opt); /* NOP options */
			out[2] = p->len >> 8;
			out[3] = p->len & 0xFF;
		}
		out[10] = 0;
		out[11] = 0;
		cs = inet_cs(out, IPV4_HDR + opt);
		out[10] = cs >> 8;
		out[11] = cs & 0xFF;
	}

	if (!(flags & F_TYPE_REQ)) {
		icmp[0] = v6 ? ICMP6_ECHO_REP : ICMP_ECHO_REP;
	}
	if (flags & F_WRONG_ID) {
		icmp[5] ^= 0x01;
	}
	if (flags & F_WRONG_SEQ) {
		icmp[7] ^= 0x01;
	}
	if (flags & F_WRONG_SRC) {
		out[v6 ? 8 + 15 : 12 + 3] ^= 0x01;
	}

	icmp[2] = 0;
	icmp[3] = 0;
	cs = v6 ? icmp6_cs(out, icmp, icmp_len) : inet_cs(icmp, icmp_len);
	icmp[2] = cs >> 8;
	icmp[3] = cs & 0xFF;
	if (flags & F_BAD_CSUM) {
		icmp[3] ^= 0xFF;
	}
}

static void push_pkt(const struct pkt *p)
{
	queue[q_tail % QUEUE_LEN] = *p;
	q_tail++;
}

/* Unrelated traffic: a UDP datagram */
static void push_junk(int32_t delay_ms)
{
	struct pkt p = {.len = 28, .delay_ms = delay_ms};

	p.data[0] = 0x45;
	p.data[3] = 28;
	p.data[9] = 17;
	push_pkt(&p);
}

/* zsock stubs */
static int socket_stub(int family, int type, int proto, int num_calls)
{
	socket_calls++;
	return 5;
}

static int setsockopt_stub(int sock, int level, int optname, const void *optval,
			   net_socklen_t optlen, int num_calls)
{
	return 0;
}

static int close_stub(int sock, int num_calls)
{
	return 0;
}

static ssize_t send_stub(int sock, const void *buf, size_t len, int flags, int num_calls)
{
	const uint8_t *req = buf;
	bool v6 = (req[0] >> 4) == 6;
	int ip_hdr = v6 ? IPV6_HDR : IPV4_HDR;
	int idx = req_count++;

	memcpy(last_req, buf, len);
	last_req_len = len;
	if (idx < MAX_REQ) {
		req_id[idx] = (req[ip_hdr + 4] << 8) | req[ip_hdr + 5];
		req_seq[idx] = (req[ip_hdr + 6] << 8) | req[ip_hdr + 7];

		for (int i = 0; i < script_len[idx]; i++) {
			struct pkt p;

			build_reply(req, len, script[idx][i].flags, &p);
			p.delay_ms = script[idx][i].delay_ms;
			push_pkt(&p);
		}
	}
	return len;
}

static int poll_stub(struct zsock_pollfd *fds, int nfds, int timeout, int num_calls)
{
	struct pkt *p;

	if (poll_timeout_min == 0 || timeout < poll_timeout_min) {
		poll_timeout_min = timeout;
	}

	if (q_head == q_tail) {
		if (junk_gap_ms <= 0) {
			k_sleep(K_MSEC(timeout));
			return 0;
		}
		push_junk(junk_gap_ms);
	}

	p = &queue[q_head % QUEUE_LEN];
	if (p->delay_ms > timeout) {
		k_sleep(K_MSEC(timeout));
		p->delay_ms -= timeout;
		return 0;
	}
	if (p->delay_ms > 0) {
		k_sleep(K_MSEC(p->delay_ms));
		p->delay_ms = 0;
	}
	fds[0].revents = ZSOCK_POLLIN;
	return 1;
}

static ssize_t recv_stub(int sock, void *buf, size_t max_len, int flags, int num_calls)
{
	struct pkt *p;
	int len;

	if (q_head == q_tail) {
		errno = EAGAIN;
		return -1;
	}
	p = &queue[q_head % QUEUE_LEN];
	q_head++;
	len = MIN(p->len, (int)max_len);
	memcpy(buf, p->data, len);
	return len;
}

static int getaddrinfo_stub(const char *host, const char *service,
			    const struct zsock_addrinfo *hints, struct zsock_addrinfo **res,
			    int num_calls)
{
	if (strcmp(host, local_str) == 0) {
		if (gai_src_ret) {
			return gai_src_ret;
		}
		*res = &local_ai.ai;
	} else {
		if (gai_dest_ret) {
			return gai_dest_ret;
		}
		*res = &dest_ai.ai;
	}
	return 0;
}

/* The addrinfo pair is released exactly once when a ping ends or is rejected */
static void freeaddrinfo_stub(struct zsock_addrinfo *ai, int num_calls)
{
	k_sem_give(&freed_sem);
}

static int inet_pton_stub(net_sa_family_t family, const char *src, void *dst, int num_calls)
{
	/* Just enough to tell the families apart */
	if (family == NET_AF_INET6) {
		return strchr(src, ':') ? 1 : 0;
	}
	return strchr(src, ':') ? 0 : 1;
}

void setUp(void)
{
	q_head = 0;
	q_tail = 0;
	junk_gap_ms = 0;
	memset(script, 0, sizeof(script));
	memset(script_len, 0, sizeof(script_len));
	last_req_len = 0;
	req_count = 0;
	socket_calls = 0;
	poll_timeout_min = 0;
	gai_dest_ret = 0;
	gai_src_ret = 0;
	k_sem_reset(&freed_sem);
	clear_captured_response();
	use_v4();

	__cmock_zsock_socket_Stub(socket_stub);
	__cmock_zsock_setsockopt_Stub(setsockopt_stub);
	__cmock_zsock_close_Stub(close_stub);
	__cmock_zsock_send_Stub(send_stub);
	__cmock_zsock_poll_Stub(poll_stub);
	__cmock_zsock_recv_Stub(recv_stub);
	__cmock_zsock_getaddrinfo_Stub(getaddrinfo_stub);
	__cmock_zsock_freeaddrinfo_Stub(freeaddrinfo_stub);
	__cmock_zsock_inet_pton_Stub(inet_pton_stub);
}

void tearDown(void)
{
	__cmock_zsock_socket_Stub(NULL);
	__cmock_zsock_setsockopt_Stub(NULL);
	__cmock_zsock_close_Stub(NULL);
	__cmock_zsock_send_Stub(NULL);
	__cmock_zsock_poll_Stub(NULL);
	__cmock_zsock_recv_Stub(NULL);
	__cmock_zsock_getaddrinfo_Stub(NULL);
	__cmock_zsock_freeaddrinfo_Stub(NULL);
	__cmock_zsock_inet_pton_Stub(NULL);
}

/* Helpers */

static void run_ping(const char *cmd)
{
	clear_captured_response();
	k_sem_reset(&freed_sem);
	/* URCs of an earlier ping leave tx_done credits that would make send_at_command() return
	 * before the response to this command exists.
	 */
	uart_stub_tx_done_reset();
	send_at_command(cmd);
}

/* Wait until ping_task() has finished. It releases the two addrinfo. */
static bool wait_done(void)
{
	return k_sem_take(&freed_sem, K_SECONDS(120)) == 0 &&
	       k_sem_take(&freed_sem, K_SECONDS(1)) == 0;
}

static bool wait_for_response(const char *needle, int timeout_ms)
{
	for (int t = 0; t < timeout_ms; t += 50) {
		if (strstr(get_captured_response(), needle)) {
			return true;
		}
		k_sleep(K_MSEC(50));
	}
	return false;
}

/* Let a URC that is not going to arrive have its chance to */
static void settle(void)
{
	k_sleep(K_SECONDS(3));
}

/* Round trip time of the first reported echo reply, in milliseconds, or -1 */
static int first_rtt_ms(void)
{
	const char *p = strstr(get_captured_response(), "#XPING: ");
	unsigned int s;
	unsigned int ms;

	while (p) {
		if (sscanf(p, "#XPING: %u.%03u seconds", &s, &ms) == 2) {
			return s * 1000 + ms;
		}
		p = strstr(p + 1, "#XPING: ");
	}
	return -1;
}

/* The reported average in milliseconds, or -1 */
static int average_ms(void)
{
	const char *p = strstr(get_captured_response(), "#XPING: average ");
	unsigned int sec;
	unsigned int ms;

	if (p && sscanf(p, "#XPING: average %u.%03u seconds", &sec, &ms) == 2) {
		return sec * 1000 + ms;
	}
	return -1;
}

/* Wait for the average and check it. Each reply is reported up to a millisecond later than it
 * was scripted, because k_sleep() rounds up to the next tick, so a range is needed.
 */
static void assert_average(int expected_ms)
{
	TEST_ASSERT_TRUE(wait_for_response("#XPING: average ", 3000));
	TEST_ASSERT_GREATER_OR_EQUAL_INT(expected_ms, average_ms());
	TEST_ASSERT_LESS_OR_EQUAL_INT(expected_ms + 2, average_ms());
}

static void assert_ok(void)
{
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
	TEST_ASSERT_NULL(strstr(get_captured_response(), "ERROR"));
}

static void assert_error(void)
{
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));
}

/* Check that the captured request is a well formed echo request for @p payload_len. */
static void assert_request(bool v6, int payload_len)
{
	int ip_hdr = v6 ? IPV6_HDR : IPV4_HDR;
	const uint8_t *icmp = last_req + ip_hdr;
	int icmp_len = ICMP_HDR + payload_len;
	uint16_t seq = (icmp[6] << 8) | icmp[7];

	TEST_ASSERT_EQUAL_INT(ip_hdr + icmp_len, last_req_len);
	TEST_ASSERT_EQUAL_UINT8(v6 ? 6 : 4, last_req[0] >> 4);

	if (v6) {
		TEST_ASSERT_EQUAL_UINT8(58, last_req[6]);
		TEST_ASSERT_EQUAL_INT(icmp_len, (last_req[4] << 8) | last_req[5]);
		TEST_ASSERT_EQUAL_MEMORY(v6_local, last_req + 8, 16);
		TEST_ASSERT_EQUAL_MEMORY(v6_dest, last_req + 24, 16);
		TEST_ASSERT_EQUAL_UINT8(ICMP6_ECHO_REQ, icmp[0]);
		TEST_ASSERT_EQUAL_UINT16(0, icmp6_cs(last_req, icmp, icmp_len));
	} else {
		TEST_ASSERT_EQUAL_UINT8(1, last_req[9]);
		TEST_ASSERT_EQUAL_INT(last_req_len, (last_req[2] << 8) | last_req[3]);
		TEST_ASSERT_EQUAL_MEMORY(v4_local, last_req + 12, 4);
		TEST_ASSERT_EQUAL_MEMORY(v4_dest, last_req + 16, 4);
		TEST_ASSERT_EQUAL_UINT16(0, inet_cs(last_req, IPV4_HDR));
		TEST_ASSERT_EQUAL_UINT8(ICMP_ECHO_REQ, icmp[0]);
		TEST_ASSERT_EQUAL_UINT16(0, inet_cs(icmp, icmp_len));
	}

	/* The filler is a digit pattern that depends on the sequence number. It is indexed from
	 * the start of the ICMP message for IPv4 and from the start of the payload for IPv6.
	 */
	for (int i = 0; i < payload_len; i++) {
		int idx = v6 ? i : i + ICMP_HDR;

		TEST_ASSERT_EQUAL_UINT8((idx + seq) % 10 + '0', icmp[ICMP_HDR + i]);
	}
}

/* Valid replies */

void test_ipv4_echo_reply(void)
{
	script_add(0, F_NONE, 20);
	run_ping("AT#XPING=\"1.2.3.4\",32,1000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));

	assert_request(false, 32);
	TEST_ASSERT_EQUAL_INT(1, req_count);
	TEST_ASSERT_GREATER_OR_EQUAL_INT(20, first_rtt_ms());
	TEST_ASSERT_LESS_OR_EQUAL_INT(25, first_rtt_ms());
}

void test_ipv6_echo_reply(void)
{
	use_v6();
	script_add(0, F_NONE, 20);
	run_ping("AT#XPING=\"2001:DB8::1\",32,1000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));

	assert_request(true, 32);
	TEST_ASSERT_GREATER_OR_EQUAL_INT(20, first_rtt_ms());
	TEST_ASSERT_LESS_OR_EQUAL_INT(25, first_rtt_ms());
}

void test_odd_payload_length(void)
{
	/* The ICMP checksum covers a trailing odd byte */
	script_add(0, F_NONE, 5);
	run_ping("AT#XPING=\"1.2.3.4\",33,1000\r\n");
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));
	assert_request(false, 33);
}

void test_zero_length_payload(void)
{
	script_add(0, F_NONE, 5);
	run_ping("AT#XPING=\"1.2.3.4\",0,1000\r\n");
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));
	assert_request(false, 0);
}

/* Packets that are not the reply must not end the wait, but must not be accepted either.
 * Each is followed by the real reply, which arrives 40 ms after the request.
 */
static void ignored_then_valid(bool v6, unsigned int flags)
{
	if (v6) {
		use_v6();
	}
	script_add(0, flags, 10);
	script_add(0, F_NONE, 30);
	run_ping(v6 ? "AT#XPING=\"2001:DB8::1\",32,1000\r\n" : "AT#XPING=\"1.2.3.4\",32,1000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));

	/* Accepting the first packet would report 10 ms */
	TEST_ASSERT_GREATER_OR_EQUAL_INT(40, first_rtt_ms());
	TEST_ASSERT_LESS_OR_EQUAL_INT(45, first_rtt_ms());
}

void test_ipv4_bad_checksum_is_ignored(void)
{
	ignored_then_valid(false, F_BAD_CSUM);
}

void test_ipv6_bad_checksum_is_ignored(void)
{
	ignored_then_valid(true, F_BAD_CSUM);
}

void test_ipv4_wrong_sequence_is_ignored(void)
{
	ignored_then_valid(false, F_WRONG_SEQ);
}

void test_ipv6_wrong_sequence_is_ignored(void)
{
	ignored_then_valid(true, F_WRONG_SEQ);
}

/* Timeouts */

void test_no_reply_times_out(void)
{
	int64_t t0 = k_uptime_get();

	run_ping("AT#XPING=\"1.2.3.4\",32,1000,1,0\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_LESS_THAN_INT64(1100, k_uptime_get() - t0);
	settle();
	TEST_ASSERT_NULL(strstr(get_captured_response(), "#XPING"));
}

void test_unrelated_traffic_does_not_extend_timeout(void)
{
	int64_t t0;

	/* A packet every 400 ms, less than the timeout of 1000 ms */
	junk_gap_ms = 400;
	t0 = k_uptime_get();
	run_ping("AT#XPING=\"1.2.3.4\",32,1000,1,0\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_LESS_THAN_INT64(1100, k_uptime_get() - t0);
}

void test_forged_replies_do_not_extend_timeout(void)
{
	int64_t t0 = k_uptime_get();

	for (int i = 0; i < MAX_STEPS; i++) {
		script_add(0, F_BAD_CSUM, 400);
	}
	run_ping("AT#XPING=\"1.2.3.4\",32,1000,1,0\r\n");
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_LESS_THAN_INT64(1100, k_uptime_get() - t0);
}

void test_reply_after_deadline_is_a_loss(void)
{
	script_add(0, F_NONE, 1500);
	run_ping("AT#XPING=\"1.2.3.4\",32,1000\r\n");
	TEST_ASSERT_TRUE(wait_done());
	settle();
	TEST_ASSERT_NULL(strstr(get_captured_response(), "#XPING"));
}

void test_timeout_above_int32_max(void)
{
	script_add(0, F_NONE, 20);
	run_ping("AT#XPING=\"1.2.3.4\",32,3000000000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));
	TEST_ASSERT_GREATER_THAN_INT(0, poll_timeout_min);
}

/* Statistics */

void test_statistics(void)
{
	script_add(0, F_NONE, 10);
	script_add(1, F_NONE, 20);
	script_add(2, F_NONE, 30);
	run_ping("AT#XPING=\"1.2.3.4\",32,1000,3,100\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	assert_average(20);
	TEST_ASSERT_EQUAL_INT(3, req_count);
}

void test_reply_within_a_millisecond_is_received(void)
{
	/* An RTT of 0 ms is a reply, not a loss */
	script_add(0, F_NONE, 0);
	script_add(1, F_NONE, 0);
	run_ping("AT#XPING=\"1.2.3.4\",32,1000,2,0\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: average 0.000 seconds", 3000));
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "#XPING: 0.000 seconds"));
}

void test_lost_requests_are_left_out_of_the_average(void)
{
	script_add(0, F_NONE, 10);
	script_add(2, F_NONE, 30);
	run_ping("AT#XPING=\"1.2.3.4\",32,200,3,0\r\n");
	TEST_ASSERT_TRUE(wait_done());
	assert_average(20);
}

/* Identifier and sequence number */

void test_identifier_and_sequence(void)
{
	script_add(0, F_NONE, 5);
	script_add(1, F_NONE, 5);
	script_add(2, F_NONE, 5);
	run_ping("AT#XPING=\"1.2.3.4\",8,1000,3,0\r\n");
	TEST_ASSERT_TRUE(wait_done());

	TEST_ASSERT_EQUAL_INT(3, req_count);
	/* One identifier per command, and a sequence number that advances for each request */
	TEST_ASSERT_EQUAL_UINT16(req_id[0], req_id[1]);
	TEST_ASSERT_EQUAL_UINT16(req_id[0], req_id[2]);
	TEST_ASSERT_EQUAL_UINT16((uint16_t)(req_seq[0] + 1), req_seq[1]);
	TEST_ASSERT_EQUAL_UINT16((uint16_t)(req_seq[0] + 2), req_seq[2]);
}

/* Validation and error paths */

void test_ipv4_payload_length_limit(void)
{
	script_add(0, F_NONE, 5);
	run_ping("AT#XPING=\"1.2.3.4\",1472,1000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));
	assert_request(false, 1472);

	run_ping("AT#XPING=\"1.2.3.4\",1473,1000\r\n");
	assert_error();
	/* Rejected before anything is sent, with the addrinfo released */
	TEST_ASSERT_EQUAL_INT(1, req_count);
	TEST_ASSERT_EQUAL_INT(1, socket_calls);
	TEST_ASSERT_EQUAL_INT(0, k_sem_take(&freed_sem, K_NO_WAIT));
}

void test_ipv6_payload_length_limit(void)
{
	use_v6();
	script_add(0, F_NONE, 5);
	run_ping("AT#XPING=\"2001:DB8::1\",1452,1000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));
	assert_request(true, 1452);

	run_ping("AT#XPING=\"2001:DB8::1\",1453,1000\r\n");
	assert_error();
	TEST_ASSERT_EQUAL_INT(1, req_count);
	TEST_ASSERT_EQUAL_INT(1, socket_calls);
}

void test_rejected_command_does_not_reach_the_queued_ping(void)
{
	/* The first command is queued but cannot run before the second is parsed. A rejected
	 * second command must leave the first one's request untouched.
	 */
	script_add(0, F_NONE, 5);
	clear_captured_response();
	k_sem_reset(&freed_sem);
	send_at_command("AT#XPING=\"1.2.3.4\",8,1000\r\nAT#XPING=\"1.2.3.4\",65000,1000\r\n");
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_EQUAL_INT(1, req_count);
	TEST_ASSERT_EQUAL_INT(IPV4_HDR + ICMP_HDR + 8, last_req_len);
}

void test_zero_count_is_rejected(void)
{
	run_ping("AT#XPING=\"1.2.3.4\",32,1000,0\r\n");
	assert_error();
	TEST_ASSERT_EQUAL_INT(0, socket_calls);
}

void test_destination_resolution_failure(void)
{
	gai_dest_ret = GAI_FAIL;
	run_ping("AT#XPING=\"example.invalid\",32,1000\r\n");
	assert_error();
	TEST_ASSERT_EQUAL_INT(0, socket_calls);

	/* A failed command does not leave the ping busy */
	gai_dest_ret = 0;
	script_add(0, F_NONE, 5);
	run_ping("AT#XPING=\"1.2.3.4\",32,1000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));
}

void test_source_resolution_failure(void)
{
	gai_src_ret = GAI_FAIL;
	/* The command returns the negated getaddrinfo() error. The AT host takes a positive value
	 * for the return value of nrf_modem_at_cmd() and decodes its type and value. It is not a
	 * CME or CMS error.
	 */
	__cmock_nrf_modem_at_err_type_IgnoreAndReturn(NRF_MODEM_AT_ERROR);
	__cmock_nrf_modem_at_err_IgnoreAndReturn(-GAI_FAIL);
	run_ping("AT#XPING=\"1.2.3.4\",32,1000\r\n");
	/* A plain ERROR, not an encoded modem error */
	assert_error();
	TEST_ASSERT_NULL(strstr(get_captured_response(), "CME"));
	TEST_ASSERT_EQUAL_INT(0, socket_calls);
	/* The destination addrinfo is released */
	TEST_ASSERT_EQUAL_INT(0, k_sem_take(&freed_sem, K_NO_WAIT));

	gai_src_ret = 0;
	script_add(0, F_NONE, 5);
	run_ping("AT#XPING=\"1.2.3.4\",32,1000\r\n");
	assert_ok();
	TEST_ASSERT_TRUE(wait_done());
	TEST_ASSERT_TRUE(wait_for_response("#XPING: 0.0", 3000));
}

void test_no_local_ipv6_address(void)
{
	/* An IPv6 target on a PDN that only has an IPv4 address */
	use_v6();
	test_cgpaddr_response = "+CGPADDR: 0,\"10.0.0.2\"\r\nOK\r\n";
	run_ping("AT#XPING=\"2001:DB8::1\",32,1000\r\n");
	assert_error();
	TEST_ASSERT_EQUAL_INT(0, socket_calls);
}

void test_no_local_ipv4_address(void)
{
	test_cgpaddr_response = NULL;
	run_ping("AT#XPING=\"1.2.3.4\",32,1000\r\n");
	assert_error();
	TEST_ASSERT_EQUAL_INT(0, socket_calls);
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
