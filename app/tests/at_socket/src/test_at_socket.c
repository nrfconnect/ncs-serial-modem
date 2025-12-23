/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "unity.h"
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "nrf_socket.h"
#include "sm_at_host.h"
#include "sm_at_socket.h"

/* CMock-generated mocks */
#include "cmock_nrf_socket.h"
#include "cmock_nrf_modem_at.h"
#include "zephyr/net/cmock_socket.h"

/* Minimal DNS error codes for tests */
#ifndef DNS_EAI_NONAME
#define DNS_EAI_NONAME (-2)
#endif
#ifndef DNS_EAI_SERVICE
#define DNS_EAI_SERVICE (-8)
#endif

/* Forward declaration for response capture */
extern const char *get_captured_response(void);
extern size_t get_captured_response_len(void);
extern void clear_captured_response(void);

struct k_work_q sm_work_q;

/* Helper callbacks for mocking nrf_getsockopt with output parameters */
static int mock_getsockopt_timeval_callback(int socket, int level, int option_name,
					    void *option_value, socklen_t *option_len,
					    int num_calls)
{
	struct timeval *tmo;

	/* Check if this is a timeout option by checking the length */
	if (*option_len >= sizeof(struct timeval)) {
		tmo = (struct timeval *)option_value;
		/* Return 30 or 60 seconds based on call number */
		tmo->tv_sec = (num_calls == 0) ? 30 : 60;
		tmo->tv_usec = 0;
		*option_len = sizeof(struct timeval);
	}
	return 0;
}

static int mock_getsockopt_int_callback(int socket, int level, int option_name, void *option_value,
					socklen_t *option_len, int num_calls)
{
	int *value = (int *)option_value;

	/* Return value of 1 for any integer option (peer verify, session cache, etc) */
	if (*option_len >= sizeof(int)) {
		*value = 1;
		*option_len = sizeof(int);
	}
	return 0;
}

static int mock_getsockopt_hostname_callback(int socket, int level, int option_name,
					     void *option_value, socklen_t *option_len,
					     int num_calls)
{
	char *hostname = (char *)option_value;
	const char *test_hostname = "test.server.com";

	/* Check if buffer is large enough */
	if (*option_len >= strlen(test_hostname) + 1) {
		strcpy(hostname, test_hostname);
		*option_len = strlen(test_hostname) + 1;
	}
	return 0;
}

void setUp(void)
{
	/* This is run before EACH test */
	clear_captured_response();
}

void tearDown(void)
{
	/* This is run after EACH test */
	/* Reset any stubs to prevent interference between tests */
	__cmock_nrf_getsockopt_Stub(NULL);
}

/*
 * Helper function to send AT command via sm_at_receive()
 * This simulates receiving AT command bytes over UART
 */
static void send_at_command(const char *at_cmd)
{
	bool stop_at_receive = false;
	const uint8_t *cmd_bytes = (const uint8_t *)at_cmd;
	size_t cmd_len = strlen(at_cmd);

	/* Send command through sm_at_receive() */
	sm_at_receive(cmd_bytes, cmd_len, &stop_at_receive);
}

/*
 * Test: Socket initialization
 * - Verifies that socket module initializes correctly
 */
void test_socket_init(void)
{
	int ret;

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);
}

/*
 * Test: Socket initialization is idempotent
 * - Multiple calls to init should not fail
 */
void test_socket_init_multiple(void)
{
	int ret;

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);
}

/*
 * Test: Create IPv4 TCP socket via AT command
 * - Command: AT#XSOCKET=1,1,0\r\n
 * - Tests: IPv4, TCP, Client role
 */
void test_xsocket_ipv4_tcp(void)
{
	int ret;
	const char *response;

	/* Initialize AT host and socket subsystem */
	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 3 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command via sm_at_receive() */
	send_at_command("AT#XSOCKET=1,1,0\r\n");

	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "3,1,6") !=
			 NULL); /* handle=3, type=1(STREAM), proto=6(TCP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create IPv4 UDP socket via AT command
 * - Command: AT#XSOCKET=1,2,0\r\n
 * - Tests: IPv4, UDP, Client role
 */
void test_xsocket_ipv4_udp(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 4 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 4);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command via sm_at_receive() */
	send_at_command("AT#XSOCKET=1,2,0\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "4,2,17") !=
			 NULL); /* handle=4, type=2(DGRAM), proto=17(UDP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create IPv6 TCP socket via AT command
 * - Command: AT#XSOCKET=2,1,0\r\n
 * - Tests: IPv6, TCP, Client role
 */
void test_xsocket_ipv6_tcp(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 5 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 5);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command via sm_at_receive() */
	send_at_command("AT#XSOCKET=2,1,0\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "5,1,6") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(5, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create RAW socket via AT command
 * - Command: AT#XSOCKET=1,3,0\r\n
 * - Tests: RAW socket type
 */
void test_xsocket_raw(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 6 for RAW socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_SOCK_RAW, NRF_SOCK_RAW, NRF_IPPROTO_RAW, 6);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command: family=1(IPv4), type=3(RAW), role=0(client) */
	send_at_command("AT#XSOCKET=1,3,0\r\n");

	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "6,3,0") !=
			 NULL); /* handle=6, type=3(RAW), proto=0(IP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(6, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Attempt to create socket with invalid type
 * - Command: AT#XSOCKET=1,99,0\r\n
 * - Tests: Invalid socket type rejection
 */
void test_xsocket_invalid_type(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Send AT command with invalid socket type 99 */
	send_at_command("AT#XSOCKET=1,99,0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create maximum number of sockets
 * - Tests that the application can create up to CONFIG_POSIX_OPEN_MAX-1 sockets
 * - Verifies that attempting to create one more socket fails
 */
void test_xsocket_max_sockets(void)
{
	int ret;
	const char *response;
	int max_sockets = CONFIG_POSIX_OPEN_MAX - 1;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create 3 sockets (determined by CONFIG_POSIX_OPEN_MAX - 1) */
	for (int i = 0; i < max_sockets; i++) {

		/* Mock nrf_socket to return fd */
		__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP,
						   i);

		/* Mock setsockopt calls (send timeout, poll callback) */
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

		/* Send AT command via sm_at_receive() */
		send_at_command("AT#XSOCKET=1,1,0\r\n");

		/* Verify response contains socket handle */
		response = get_captured_response();
		TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
		/* Note: OK response is sent separately and not captured in test */

		clear_captured_response();
	}
	printk("Created %d sockets successfully.\n", max_sockets);

	/* Attempt to create a 4th socket - should fail */
	send_at_command("AT#XSOCKET=1,1,0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Clean up: close all 3 sockets */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_close_ExpectAndReturn(i, 0);
	}

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create socket with specific PDN context
 * - Command: AT#XSOCKET=<family>,<type>,<role>,<cid>\r\n
 * - Tests: Binding to specific PDN context
 */
void test_xsocket_with_pdn_cid(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 6 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 6);

	/* Mock setsockopt calls */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */

	/* Mock AT%XGETPDNID command for PDN ID retrieval (cid=1 -> pdn_id=1) */
	const char *pdn_id_resp = "%XGETPDNID: 1\r\nOK\r\n";

	__cmock_nrf_modem_at_cmd_CMockExpectAnyArgsAndReturn(__LINE__, 0);
	__cmock_nrf_modem_at_cmd_CMockReturnMemThruPtr_buf(__LINE__, (void *)pdn_id_resp,
							   strlen(pdn_id_resp) + 1);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_len(__LINE__);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_fmt(__LINE__);

	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_BINDTOPDN */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll callback */

	/* Send AT command: family=1(IPv4), type=1(STREAM), role=0(client), cid=1 */
	send_at_command("AT#XSOCKET=1,1,0,1\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "6,1,6") !=
			 NULL); /* handle=6, type=1(STREAM), proto=6(TCP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(6, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Socket bind operation via AT command
 * - Command: AT#XBIND=<handle>,<port>\r\n
 * - Tests bind functionality
 */
void test_xbind_operation(void)
{
	int ret;
	const char *response;
	/* Mock modem response for AT+CGPADDR using ReturnMemThruPtr */
	const char *cgpaddr_resp = "+CGPADDR: 3,\"127.0.0.1\",\"\"\r\nOK\r\n";

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "3,2,17") !=
			 NULL); /* handle=3, type=2(DGRAM), proto=17(UDP) */

	clear_captured_response();

	/* Bind operation - query modem IP, inet_pton, nrf_bind */
	__cmock_nrf_modem_at_cmd_CMockExpectAnyArgsAndReturn(__LINE__, 0);
	__cmock_nrf_modem_at_cmd_CMockReturnMemThruPtr_buf(__LINE__, (void *)cgpaddr_resp,
							   strlen(cgpaddr_resp) + 1);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_len(__LINE__);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_fmt(__LINE__);
	/* util_get_ip_addr() validates the IP with zsock_inet_pton */
	__cmock_zsock_inet_pton_ExpectAnyArgsAndReturn(1);
	/* bind_to_local_addr() converts the IP with nrf_inet_pton */
	__cmock_nrf_inet_pton_ExpectAnyArgsAndReturn(1);
	__cmock_nrf_bind_ExpectAndReturn(3, NULL, sizeof(struct nrf_sockaddr_in), 0);
	__cmock_nrf_bind_IgnoreArg_address();
	__cmock_nrf_bind_IgnoreArg_address_len();

	/* Execute bind command via sm_at_receive() */
	send_at_command("AT#XBIND=3,8080\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Socket bind operation with IPv6 via AT command
 * - Command: AT#XBIND=<handle>,<port>\r\n
 * - Tests IPv6 bind functionality
 */
void test_xbind_ipv6_operation(void)
{
	int ret;
	const char *response;
	/* Mock modem response for AT+CGPADDR with IPv6 address in first position */
	const char *cgpaddr_resp = "+CGPADDR: 3,\"2001:db8::1\"\r\nOK\r\n";

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create IPv6 UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=2,2,0\r\n");
	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "3,2,17") !=
			 NULL); /* handle=3, type=2(DGRAM), proto=17(UDP) */

	clear_captured_response();

	/* Bind operation - query modem IP, inet_pton, nrf_bind */
	__cmock_nrf_modem_at_cmd_CMockExpectAnyArgsAndReturn(__LINE__, 0);
	__cmock_nrf_modem_at_cmd_CMockReturnMemThruPtr_buf(__LINE__, (void *)cgpaddr_resp,
							   strlen(cgpaddr_resp) + 1);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_len(__LINE__);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_fmt(__LINE__);
	/* util_get_ip_addr() validates the IP with zsock_inet_pton */
	__cmock_zsock_inet_pton_ExpectAnyArgsAndReturn(1);
	/* bind_to_local_addr() converts the IP with nrf_inet_pton */
	__cmock_nrf_inet_pton_ExpectAnyArgsAndReturn(1);
	__cmock_nrf_bind_ExpectAndReturn(3, NULL, sizeof(struct nrf_sockaddr_in6), 0);
	__cmock_nrf_bind_IgnoreArg_address();
	__cmock_nrf_bind_IgnoreArg_address_len();

	/* Execute bind command via sm_at_receive() */
	send_at_command("AT#XBIND=3,8080\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/* Negative-path: bind fails when no IP address available from modem */
void test_xbind_invalid_ip(void)
{
	int ret;
	const char *response;
	/* Mock modem response with no IP address */
	const char *cgpaddr_resp = "+CGPADDR: 3,\"\",\"\"\r\nOK\r\n";

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Ensure socket subsystem is initialized and handle 3 exists */
	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock AT+CGPADDR to return empty IP address (network down scenario) */
	__cmock_nrf_modem_at_cmd_CMockExpectAnyArgsAndReturn(__LINE__, 0);
	__cmock_nrf_modem_at_cmd_CMockReturnMemThruPtr_buf(__LINE__, (void *)cgpaddr_resp,
							   strlen(cgpaddr_resp) + 1);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_len(__LINE__);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_fmt(__LINE__);

	/* Bind fails because no valid IP address available */
	send_at_command("AT#XBIND=3,1234\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Cleanup */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/* Negative-path: bind with out-of-range port */
void test_xbind_invalid_port(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Ensure socket subsystem is initialized and handle 3 exists */
	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Port > 65535 fails AT parser validation (out of range) */
	send_at_command("AT#XBIND=3,70000\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Cleanup */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/* Negative-path: connect with malformed IP */
void test_xconnect_invalid_ip(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Ensure socket subsystem is initialized and handle 3 exists */
	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* zsock_getaddrinfo will be invoked and should fail for malformed IP */
	__cmock_zsock_getaddrinfo_CMockExpectAnyArgsAndReturn(__LINE__, DNS_EAI_NONAME);
	/* Allow any number of gai_strerror calls */
	__cmock_zsock_gai_strerror_CMockIgnoreAndReturn(__LINE__, "mock");
	__cmock_zsock_getaddrinfo_CMockIgnoreArg_hints(__LINE__);
	__cmock_zsock_getaddrinfo_CMockIgnoreArg_res(__LINE__);

	send_at_command("AT#XCONNECT=3,\"xyz\",80\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Cleanup */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/* Negative-path: connect with zero/negative port */
void test_xconnect_invalid_port(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Ensure socket subsystem is initialized and handle 3 exists */
	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Port 0 triggers service error in getaddrinfo */
	__cmock_zsock_getaddrinfo_CMockExpectAnyArgsAndReturn(__LINE__, DNS_EAI_SERVICE);
	__cmock_zsock_gai_strerror_CMockExpectAnyArgsAndReturn(__LINE__, "mock");
	__cmock_zsock_getaddrinfo_CMockIgnoreArg_hints(__LINE__);
	__cmock_zsock_getaddrinfo_CMockIgnoreArg_res(__LINE__);

	send_at_command("AT#XCONNECT=3,\"10.0.0.1\",0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	clear_captured_response();
	/* Negative port fails AT parser validation (out of range), so getaddrinfo is never called
	 */
	send_at_command("AT#XCONNECT=3,\"10.0.0.1\",70000\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Cleanup */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/* Helper callback for mocking successful zsock_getaddrinfo */
static int mock_getaddrinfo_success_callback(const char *host, const char *service,
					     const struct zsock_addrinfo *hints,
					     struct zsock_addrinfo **res, int num_calls)
{
	static struct {
		struct zsock_addrinfo ai;
		union {
			struct sockaddr sa;
			struct sockaddr_in sa_in;
			struct sockaddr_in6 sa_in6;
		} addr;
	} getaddrinfo_result;

	/* Setup a valid IPv4 address structure */
	memset(&getaddrinfo_result, 0, sizeof(getaddrinfo_result));
	getaddrinfo_result.addr.sa_in.sin_family = AF_INET;
	getaddrinfo_result.addr.sa_in.sin_port = htons(80);
	getaddrinfo_result.addr.sa_in.sin_addr.s_addr = htonl(0xC0A80001); /* 192.168.0.1 */

	/* Setup addrinfo structure */
	getaddrinfo_result.ai.ai_family = AF_INET;
	getaddrinfo_result.ai.ai_socktype = SOCK_STREAM;
	getaddrinfo_result.ai.ai_protocol = IPPROTO_TCP;
	getaddrinfo_result.ai.ai_addrlen = sizeof(getaddrinfo_result.addr.sa_in);
	getaddrinfo_result.ai.ai_addr = &getaddrinfo_result.addr.sa;

	*res = &getaddrinfo_result.ai;
	return 0;
}

/*
 * Test: Socket connect operation via AT command
 * - Command: AT#XCONNECT=<handle>,"<url>",<port>\r\n
 * - Tests TCP connection
 */
void test_xconnect_operation(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock getaddrinfo to succeed with valid address */
	__cmock_zsock_getaddrinfo_Stub(mock_getaddrinfo_success_callback);
	__cmock_zsock_freeaddrinfo_Expect(NULL);
	__cmock_zsock_freeaddrinfo_IgnoreArg_ai();

	/* Mock successful nrf_connect */
	__cmock_nrf_connect_ExpectAnyArgsAndReturn(0);

	/* Execute connect command via sm_at_receive() */
	send_at_command("AT#XCONNECT=3,\"test.server.com\",80\r\n");

	/* Verify successful connection response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XCONNECT: 3,1") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Close socket via AT command
 * - Command: AT#XCLOSE=<handle>\r\n (close specific socket)
 * - Command: AT#XCLOSE\r\n (close all open sockets)
 * - Tests both individual socket closure and closing all sockets at once
 */
void test_xclose_operation(void)
{
	int ret;
	const char *response;
	int max_sockets = CONFIG_POSIX_OPEN_MAX - 1;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Test 1: Close single socket using handle */
	/* Create one socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Close it using handle */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XCLOSE:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "3,0") != NULL);
	clear_captured_response();

	/* Test 2: Close several sockets with one call without handle */
	/* Create multiple sockets (maximum allowed) */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP,
						   i);
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
		send_at_command("AT#XSOCKET=1,1,0\r\n");
		clear_captured_response();
	}

	/* Close all sockets with one command (no handle parameter) */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_close_ExpectAndReturn(i, 0);
	}
	send_at_command("AT#XCLOSE\r\n");

	/* Verify response contains #XCLOSE for each socket and OK */
	response = get_captured_response();
	for (int i = 0; i < max_sockets; i++) {
		char expected[32];

		snprintf(expected, sizeof(expected), "#XCLOSE: %d,0", i);
		TEST_ASSERT_TRUE(strstr(response, expected) != NULL);
	}
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/* Helper callback for mocking nrf_inet_ntop (IPv4) */
static const char *mock_nrf_inet_ntop_ipv4_callback(int af, const void *src, char *dst,
						    nrf_socklen_t size, int num_calls)
{
	/* Return the IPv4 address string */
	strcpy(dst, "192.168.0.1");
	return dst;
}

/* Helper callback for mocking nrf_inet_ntop (IPv6) */
static const char *mock_nrf_inet_ntop_ipv6_callback(int af, const void *src, char *dst,
						    nrf_socklen_t size, int num_calls)
{
	/* Return the IPv6 address string */
	strcpy(dst, "2001:db8::1");
	return dst;
}

/* Helper callback for mocking successful nrf_getaddrinfo (IPv4) */
static int mock_nrf_getaddrinfo_ipv4_callback(const char *nodename, const char *servname,
					      const struct nrf_addrinfo *hints,
					      struct nrf_addrinfo **res, int num_calls)
{
	static struct {
		struct nrf_addrinfo ai;
		struct nrf_sockaddr_in sa;
	} result;

	/* Setup IPv4 address */
	memset(&result, 0, sizeof(result));
	result.sa.sin_family = NRF_AF_INET;
	result.sa.sin_addr.s_addr = htonl(0xC0A80001); /* 192.168.0.1 */

	/* Setup addrinfo */
	result.ai.ai_family = NRF_AF_INET;
	result.ai.ai_socktype = NRF_SOCK_STREAM;
	result.ai.ai_protocol = NRF_IPPROTO_TCP;
	result.ai.ai_addrlen = sizeof(result.sa);
	result.ai.ai_addr = (struct nrf_sockaddr *)&result.sa;
	result.ai.ai_next = NULL;

	*res = &result.ai;
	return 0;
}

/* Helper callback for mocking successful nrf_getaddrinfo (IPv6) */
static int mock_nrf_getaddrinfo_ipv6_callback(const char *nodename, const char *servname,
					      const struct nrf_addrinfo *hints,
					      struct nrf_addrinfo **res, int num_calls)
{
	static struct {
		struct nrf_addrinfo ai;
		struct nrf_sockaddr_in6 sa;
	} result;

	/* Setup IPv6 address (2001:db8::1) */
	memset(&result, 0, sizeof(result));
	result.sa.sin6_family = NRF_AF_INET6;
	result.sa.sin6_addr.s6_addr[0] = 0x20;
	result.sa.sin6_addr.s6_addr[1] = 0x01;
	result.sa.sin6_addr.s6_addr[2] = 0x0d;
	result.sa.sin6_addr.s6_addr[3] = 0xb8;
	result.sa.sin6_addr.s6_addr[15] = 0x01;

	/* Setup addrinfo */
	result.ai.ai_family = NRF_AF_INET6;
	result.ai.ai_socktype = NRF_SOCK_STREAM;
	result.ai.ai_protocol = NRF_IPPROTO_TCP;
	result.ai.ai_addrlen = sizeof(result.sa);
	result.ai.ai_addr = (struct nrf_sockaddr *)&result.sa;
	result.ai.ai_next = NULL;

	*res = &result.ai;
	return 0;
}

/*
 * Test: Resolve hostname via AT#XGETADDRINFO command (IPv4)
 * - Command: AT#XGETADDRINFO="hostname"
 * - Tests: Basic hostname resolution returning IPv4 address
 */
void test_xgetaddrinfo_ipv4(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_getaddrinfo to succeed with IPv4 address */
	__cmock_nrf_getaddrinfo_Stub(mock_nrf_getaddrinfo_ipv4_callback);
	__cmock_nrf_inet_ntop_Stub(mock_nrf_inet_ntop_ipv4_callback);
	__cmock_nrf_freeaddrinfo_Expect(NULL);
	__cmock_nrf_freeaddrinfo_IgnoreArg_ai();

	/* Execute XGETADDRINFO command */
	send_at_command("AT#XGETADDRINFO=\"example.com\"\r\n");

	/* Verify response contains IPv4 address */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XGETADDRINFO: \"192.168.0.1\"") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Resolve hostname via AT#XGETADDRINFO command (IPv6)
 * - Command: AT#XGETADDRINFO="hostname",2
 * - Tests: Hostname resolution with IPv6 address family specified
 */
void test_xgetaddrinfo_ipv6(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_getaddrinfo to succeed with IPv6 address */
	__cmock_nrf_getaddrinfo_Stub(mock_nrf_getaddrinfo_ipv6_callback);
	__cmock_nrf_inet_ntop_Stub(mock_nrf_inet_ntop_ipv6_callback);
	__cmock_nrf_freeaddrinfo_Expect(NULL);
	__cmock_nrf_freeaddrinfo_IgnoreArg_ai();

	/* Execute XGETADDRINFO command with IPv6 family (AF_INET6 = 2) */
	send_at_command("AT#XGETADDRINFO=\"ipv6.example.com\",2\r\n");

	/* Verify response contains IPv6 address */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XGETADDRINFO: \"2001:db8::1\"") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: AT#XGETADDRINFO with invalid address family
 * - Command: AT#XGETADDRINFO="hostname",99
 * - Tests: Error handling for invalid address family parameter
 */
void test_xgetaddrinfo_invalid_family(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Execute XGETADDRINFO with invalid address family (99) */
	send_at_command("AT#XGETADDRINFO=\"example.com\",99\r\n");

	/* Verify ERROR response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: AT#XGETADDRINFO when DNS resolution fails
 * - Command: AT#XGETADDRINFO="invalid.host"
 * - Tests: Error response when hostname cannot be resolved
 */
void test_xgetaddrinfo_dns_failure(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_getaddrinfo to fail with DNS_EAI_NONAME */
	__cmock_nrf_getaddrinfo_ExpectAnyArgsAndReturn(DNS_EAI_NONAME);
	__cmock_zsock_gai_strerror_CMockExpectAnyArgsAndReturn(__LINE__,
							       "Name or service not known");

	/* Execute XGETADDRINFO command */
	send_at_command("AT#XGETADDRINFO=\"invalid.host\"\r\n");

	/* Verify error response with error message */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XGETADDRINFO:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create secure IPv4 TCP socket via AT command
 * - Command: AT#XSSOCKET=1,1,0,<sec_tag>\r\n
 * - Tests: IPv4, TCP, Client role, TLS
 */
void test_xssocket_ipv4_tcp_client(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 3 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 3);

	/* Mock setsockopt calls (send timeout, sec_tag_list, peer_verify, poll callback) */
	/* Note: SO_BINDTOPDN is not called when cid=0 */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */

	/* Send AT command: family=1(IPv4), type=1(STREAM), role=0(client), sec_tag=42 */
	send_at_command("AT#XSSOCKET=1,1,0,42\r\n");

	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "3,1,258") !=
			 NULL); /* handle=3, type=1(STREAM), proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create secure IPv4 DTLS socket via AT command
 * - Command: AT#XSSOCKET=1,2,0,<sec_tag>\r\n
 * - Tests: IPv4, UDP, Client role, DTLS
 */
void test_xssocket_ipv4_dtls_client(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 4 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM,
					   273 /* NRF_SPROTO_DTLS1v2 */, 4);

	/* Mock setsockopt calls */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */

	/* Send AT command: family=1(IPv4), type=2(DGRAM), role=0(client), sec_tag=16842752 */
	send_at_command("AT#XSSOCKET=1,2,0,16842752\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "4,2,273") !=
			 NULL); /* handle=4, type=2(DGRAM), proto=273(DTLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create secure IPv6 TCP socket via AT command
 * - Command: AT#XSSOCKET=2,1,0,<sec_tag>\r\n
 * - Tests: IPv6, TCP, Client role, TLS
 */
void test_xssocket_ipv6_tcp_client(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 5 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 5);

	/* Mock setsockopt calls */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Bind to PDN */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */

	/* Send AT command: family=2(IPv6), type=1(STREAM), role=0(client), sec_tag=42 */
	send_at_command("AT#XSSOCKET=2,1,0,42\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "5,1,258") !=
			 NULL); /* handle=5, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(5, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create secure TLS server socket via AT command
 * - Command: AT#XSSOCKET=1,1,1,<sec_tag>\r\n
 * - Tests: IPv4, TCP, Server role, TLS
 */
void test_xssocket_ipv4_tcp_server(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 6 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 6);

	/* Mock setsockopt calls (server needs SO_SEC_ROLE as well) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_ROLE */

	/* Send AT command: family=1(IPv4), type=1(STREAM), role=1(server), sec_tag=42 */
	send_at_command("AT#XSSOCKET=1,1,1,42\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "6,1,258") !=
			 NULL); /* handle=6, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(6, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create secure socket with custom peer verification
 * - Command: AT#XSSOCKET=1,1,0,<sec_tag>,<peer_verify>\r\n
 * - Tests: Custom peer verification level
 */
void test_xssocket_custom_peer_verify(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 7 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 7);

	/* Mock setsockopt calls */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */

	/* Send AT command: family=1, type=1, role=0, sec_tag=42, peer_verify=0(none) */
	send_at_command("AT#XSSOCKET=1,1,0,42,0\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "7,1,258") !=
			 NULL); /* handle=7, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(7, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Create secure socket with specific PDN context
 * - Command: AT#XSSOCKET=1,1,0,<sec_tag>,<peer_verify>,<cid>\r\n
 * - Tests: Binding to specific PDN context
 */
void test_xssocket_with_pdn_cid(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Mock nrf_socket to return fd 8 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 8);

	/* Mock setsockopt calls (SO_BINDTOPDN is called because cid=1, even though pdn_id=0) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */

	/* Mock AT%XGETPDNID command for PDN ID retrieval (cid=1 -> pdn_id=1) */
	const char *pdn_id_resp = "%XGETPDNID: 1\r\nOK\r\n";

	__cmock_nrf_modem_at_cmd_CMockExpectAnyArgsAndReturn(__LINE__, 0);
	__cmock_nrf_modem_at_cmd_CMockReturnMemThruPtr_buf(__LINE__, (void *)pdn_id_resp,
							   strlen(pdn_id_resp) + 1);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_len(__LINE__);
	__cmock_nrf_modem_at_cmd_CMockIgnoreArg_fmt(__LINE__);

	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_BINDTOPDN */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */

	/* Send AT command: family=1, type=1, role=0, sec_tag=42, peer_verify=2, cid=1 */
	send_at_command("AT#XSSOCKET=1,1,0,42,2,1\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "8,1,258") !=
			 NULL); /* handle=8, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(8, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Read operation for XSOCKET listing all open non-secure sockets
 * - Command: AT#XSOCKET?\r\n
 * - Tests: Query returns details of all open non-secure sockets
 */
void test_xsocket_read_operation(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create first socket: IPv4 TCP client (fd=1) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 1") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Create second socket: IPv6 UDP client (fd=2) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=2,2,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 2") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Create third socket: IPv4 TCP server (fd=3) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,1\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 3") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Execute read operation */
	send_at_command("AT#XSOCKET?\r\n");
	response = get_captured_response();

	/* Verify response contains all three sockets with correct details */
	/* Format: #XSOCKET: <fd>,<family>,<role>,<type>,<cid> */
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 1,1,0,1,0") !=
			 NULL); /* fd=1, IPv4, client, TCP, cid=0 */
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 2,2,0,2,0") !=
			 NULL); /* fd=2, IPv6, client, UDP, cid=0 */
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 3,1,1,1,0") !=
			 NULL); /* fd=3, IPv4, server, TCP, cid=0 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close all sockets */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Read operation for XSSOCKET listing all open secure sockets
 * - Command: AT#XSSOCKET?\r\n
 * - Tests: Query returns details of all open secure sockets
 */
void test_xssocket_read_operation(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create first secure socket: IPv4 TLS client (fd=1) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSSOCKET=1,1,0,42\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET: 1") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Create second secure socket: IPv4 DTLS client (fd=2) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM,
					   273 /* NRF_SPROTO_DTLS1v2 */, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSSOCKET=1,2,0,16842752\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET: 2") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Create third secure socket: IPv6 TLS server (fd=3) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_ROLE */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSSOCKET=2,1,1,42\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET: 3") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Execute read operation */
	send_at_command("AT#XSSOCKET?\r\n");
	response = get_captured_response();

	/* Verify response contains all three secure sockets with correct details */
	/* Format: #XSSOCKET: <fd>,<family>,<role>,<type>,<sec_tag>,<cid> */
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET: 1,1,0,1,42,0") !=
			 NULL); /* fd=1, IPv4, client, TCP, sec_tag=42, cid=0 */
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET: 2,1,0,2,16842752,0") !=
			 NULL); /* fd=2, IPv4, client, UDP, sec_tag=16842752, cid=0 */
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET: 3,2,1,1,42,0") !=
			 NULL); /* fd=3, IPv6, server, TCP, sec_tag=42, cid=0 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close all sockets */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Set and get socket options for regular sockets
 * - Commands: AT#XSOCKETOPT=<handle>,1,<name>,<value> (set)
 *             AT#XSOCKETOPT=<handle>,0,<name> (get)
 * - Tests: Setting and getting SO_RCVTIMEO and SO_SNDTIMEO options
 */
void test_xsocketopt_set_get(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create a socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Bind to PDN */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 0") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set SO_RCVTIMEO (option 20) to 30 seconds */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKETOPT=0,1,20,30\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set SO_SNDTIMEO (option 21) to 60 seconds */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKETOPT=0,1,21,60\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Get SO_RCVTIMEO (option 20) - should return 30 */
	__cmock_nrf_getsockopt_Stub(mock_getsockopt_timeval_callback);
	send_at_command("AT#XSOCKETOPT=0,0,20\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKETOPT: 0,30") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Get SO_SNDTIMEO (option 21) - should return 60 */
	send_at_command("AT#XSOCKETOPT=0,0,21\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKETOPT: 0,60") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Set socket option SO_REUSEADDR (set-only)
 * - Command: AT#XSOCKETOPT=<handle>,1,2,<value>
 * - Tests: Setting SO_REUSEADDR option (option 2 is set-only)
 */
void test_xsocketopt_reuseaddr(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create a socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Bind to PDN */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 0") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set SO_REUSEADDR (option 2) to enabled (1) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKETOPT=0,1,2,1\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

/*
 * Test: Set and get secure socket options
 * - Commands: AT#XSSOCKETOPT=<handle>,1,<name>,<value> (set)
 *             AT#XSSOCKETOPT=<handle>,0,<name> (get)
 * - Tests: Setting and getting TLS_PEER_VERIFY, TLS_SESSION_CACHE, and TLS_HOSTNAME
 */
void test_xssocketopt_set_get(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	ret = sm_at_socket_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Create a secure socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Bind to PDN */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */
	send_at_command("AT#XSSOCKET=1,1,0,42\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET: 0") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set TLS_PEER_VERIFY (option 5) to optional (1) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSSOCKETOPT=0,1,5,1\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set TLS_SESSION_CACHE (option 12) to enabled (1) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSSOCKETOPT=0,1,12,1\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set TLS_HOSTNAME (option 2) to "test.server.com" */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSSOCKETOPT=0,1,2,\"test.server.com\"\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Get TLS_PEER_VERIFY (option 5) - should return 1 */
	__cmock_nrf_getsockopt_Stub(mock_getsockopt_int_callback);
	send_at_command("AT#XSSOCKETOPT=0,0,5\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKETOPT: 0,1") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Get TLS_SESSION_CACHE (option 12) - should return 1 */
	send_at_command("AT#XSSOCKETOPT=0,0,12\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKETOPT: 0,1") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Get TLS_HOSTNAME (option 2) - should return "test.server.com" */
	__cmock_nrf_getsockopt_Stub(mock_getsockopt_hostname_callback);
	send_at_command("AT#XSSOCKETOPT=0,0,2\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKETOPT: 0,test.server.com") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	ret = sm_at_socket_uninit();
	TEST_ASSERT_EQUAL_INT(0, ret);

	sm_at_host_uninit();
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
