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
 * Test: Read operation for XSOCKET listing all open non-secure sockets
 * - Command: AT#XSOCKET?\r\n
 * - Tests: Query returns details of all open non-secure sockets
 */
void test_xsocket_read_operation(void)
{
	const char *response;

	/* Create first socket: IPv4 TCP client (fd=1) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 1,1,6") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	clear_captured_response();
	/* Create second socket: IPv6 UDP client (fd=2) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=2,2,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 2,2,17") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	clear_captured_response();
	/* Create third socket: IPv4 TCP server (fd=3) */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,1\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 3,1,6") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	clear_captured_response();
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
	send_at_command("AT#XCLOSE=1\r\n");
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: AT#XSOCKET=? (TEST command type)
 * - Verifies TEST command returns syntax help for XSOCKET command
 * - Expected response includes family, type, and role options
 */
void test_xsocket_test_command(void)
{
	const char *response;

	/* Send TEST command */
	send_at_command("AT#XSOCKET=?\r\n");

	/* Verify response contains syntax help */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* Verify it contains expected values: family (1,2), type (1,2,3), role (0,1) */
	TEST_ASSERT_TRUE(strstr(response, "1,2") != NULL);  /* AT_SOCKET_OPEN, AT_SOCKET_OPEN6 */
}

/*
 * Test: AT command with invalid separator
 * - Command: AT&XSOCKET=1,1,0\r\n
 * - Tests: Invalid separator character (&) rejection
 * - Expected: Valid separators are +, %, # only
 */
void test_xsocket_invalid_separator(void)
{
	const char *response;

	/* Send AT command with invalid separator '&' instead of '#' */
	send_at_command("AT&XSOCKET=1,1,0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: Create IPv4 TCP socket via AT command
 * - Command: AT#XSOCKET=1,1,0\r\n
 * - Tests: IPv4, TCP, Client role
 */
void test_xsocket_ipv4_tcp(void)
{
	const char *response;

	/* Initialize AT host and socket subsystem */

	/* Mock nrf_socket to return fd 1 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command via sm_at_receive() */
	send_at_command("AT#XSOCKET=1,1,0\r\n");

	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,1,6") !=
			 NULL); /* handle=1, type=1(STREAM), proto=6(TCP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Create IPv4 UDP socket via AT command
 * - Command: AT#XSOCKET=1,2,0\r\n
 * - Tests: IPv4, UDP, Client role
 */
void test_xsocket_ipv4_udp(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 1 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 1);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command via sm_at_receive() */
	send_at_command("AT#XSOCKET=1,2,0\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,2,17") !=
			 NULL); /* handle=1, type=2(DGRAM), proto=17(UDP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Create IPv6 TCP socket via AT command
 * - Command: AT#XSOCKET=2,1,0\r\n
 * - Tests: IPv6, TCP, Client role
 */
void test_xsocket_ipv6_tcp(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 1 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command via sm_at_receive() */
	send_at_command("AT#XSOCKET=2,1,0\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,1,6") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Create RAW socket via AT command
 * - Command: AT#XSOCKET=3,3,0\r\n
 * - Tests: RAW socket type
 */
void test_xsocket_raw(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 0 for RAW socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_SOCK_RAW, NRF_SOCK_RAW, NRF_IPPROTO_RAW, 0);

	/* Mock setsockopt calls (send timeout, poll callback) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);

	/* Send AT command: family=1(IPv4), type=3(RAW), role=0(client) */
	send_at_command("AT#XSOCKET=3,3,0\r\n");

	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "0,3,0") !=
			 NULL); /* handle=0, type=3(RAW), proto=0(IP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/*
 * Test: Attempt to create packet/raw socket with invalid family or type
 * - Command: AT#XSOCKET=1,3,0\r\n
 * - Command: AT#XSOCKET=3,1,0\r\n
 * - Tests: Invalid socket family/type rejection
 */
void test_xsocket_raw_invalid_family_type(void)
{
	const char *response;

	/* Send AT command with packet family but not raw type */
	send_at_command("AT#XSOCKET=3,1,0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Send AT command with raw type but not packet family */
	send_at_command("AT#XSOCKET=1,3,0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: Attempt to create socket with invalid family
 * - Command: AT#XSOCKET=0,1,0\r\n
 * - Tests: Invalid socket family rejection
 */
void test_xsocket_invalid_family(void)
{
	const char *response;

	/* Send AT command with invalid socket family 0 */
	send_at_command("AT#XSOCKET=0,1,0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: Attempt to create socket with invalid type
 * - Command: AT#XSOCKET=1,99,0\r\n
 * - Tests: Invalid socket type rejection
 */
void test_xsocket_invalid_type(void)
{
	const char *response;

	/* Send AT command with invalid socket type 99 */
	send_at_command("AT#XSOCKET=1,99,0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: Create maximum number of sockets
 * - Tests that the application can create up to CONFIG_POSIX_OPEN_MAX-1 sockets
 * - Verifies that attempting to create one more socket fails
 */
void test_xsocket_max_sockets(void)
{
	const char *response;
	int max_sockets = CONFIG_POSIX_OPEN_MAX - 1;

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
	send_at_command("AT#XCLOSE=0\r\n");
	send_at_command("AT#XCLOSE=1\r\n");
	send_at_command("AT#XCLOSE=2\r\n");
}

/*
 * Test: Create socket with specific PDN context
 * - Command: AT#XSOCKET=<family>,<type>,<role>,<cid>\r\n
 * - Tests: Binding to specific PDN context
 */
void test_xsocket_with_pdn_cid(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 4 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 4);

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
	TEST_ASSERT_TRUE(strstr(response, "4,1,6") !=
			 NULL); /* handle=4, type=1(STREAM), proto=6(TCP) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Socket bind operation via AT command
 * - Command: AT#XBIND=<handle>,<port>\r\n
 * - Tests bind functionality
 */
void test_xbind_operation(void)
{
	const char *response;
	/* Mock modem response for AT+CGPADDR using ReturnMemThruPtr */
	const char *cgpaddr_resp = "+CGPADDR: 3,\"127.0.0.1\",\"\"\r\nOK\r\n";

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "0,2,17") !=
			 NULL); /* handle=0, type=2(DGRAM), proto=17(UDP) */

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
	__cmock_nrf_bind_ExpectAndReturn(0, NULL, sizeof(struct nrf_sockaddr_in), 0);
	__cmock_nrf_bind_IgnoreArg_address();
	__cmock_nrf_bind_IgnoreArg_address_len();

	/* Execute bind command via sm_at_receive() */
	send_at_command("AT#XBIND=0,8080\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/*
 * Test: Socket bind operation with IPv6 via AT command
 * - Command: AT#XBIND=<handle>,<port>\r\n
 * - Tests IPv6 bind functionality
 */
void test_xbind_ipv6_operation(void)
{
	const char *response;
	/* Mock modem response for AT+CGPADDR with IPv6 address in first position */
	const char *cgpaddr_resp = "+CGPADDR: 3,\"2001:db8::1\"\r\nOK\r\n";

	/* Create IPv6 UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=2,2,0\r\n");
	/* Verify response contains socket handle and correct type/protocol */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "4,2,17") !=
			 NULL); /* handle=4, type=2(DGRAM), proto=17(UDP) */

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
	__cmock_nrf_bind_ExpectAndReturn(4, NULL, sizeof(struct nrf_sockaddr_in6), 0);
	__cmock_nrf_bind_IgnoreArg_address();
	__cmock_nrf_bind_IgnoreArg_address_len();

	/* Execute bind command via sm_at_receive() */
	send_at_command("AT#XBIND=4,8080\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/* Negative-path: bind fails when no IP address available from modem */
void test_xbind_invalid_ip(void)
{
	const char *response;
	/* Mock modem response with no IP address */
	const char *cgpaddr_resp = "+CGPADDR: 3,\"\",\"\"\r\nOK\r\n";

	/* Ensure socket subsystem is initialized and handle 3 exists */

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
	send_at_command("AT#XCLOSE=3\r\n");
}

/* Negative-path: bind with out-of-range port */
void test_xbind_invalid_port(void)
{
	const char *response;

	/* Ensure socket subsystem is initialized and handle 3 exists */

	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Port > 65535 fails AT parser validation (out of range) */
	send_at_command("AT#XBIND=0,70000\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Cleanup */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/* Negative-path: connect with malformed IP */
void test_xconnect_invalid_ip(void)
{
	const char *response;

	/* Ensure socket subsystem is initialized and handle 3 exists */

	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
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

	send_at_command("AT#XCONNECT=0,\"xyz\",80\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Cleanup */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/* Negative-path: connect with out-of-range port */
void test_xconnect_invalid_port(void)
{
	const char *response;

	/* Ensure socket subsystem is initialized and handle 3 exists */

	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Port 0 triggers service error in getaddrinfo */
	__cmock_zsock_getaddrinfo_CMockExpectAnyArgsAndReturn(__LINE__, DNS_EAI_NONAME);
	__cmock_zsock_gai_strerror_CMockExpectAnyArgsAndReturn(__LINE__, "mock");
	__cmock_zsock_getaddrinfo_CMockIgnoreArg_hints(__LINE__);
	__cmock_zsock_getaddrinfo_CMockIgnoreArg_res(__LINE__);

	send_at_command("AT#XCONNECT=0,\"10.0.0.1\",0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	clear_captured_response();
	send_at_command("AT#XCONNECT=0,\"10.0.0.1\",70000\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Cleanup */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
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
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
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
	send_at_command("AT#XCONNECT=1,\"test.server.com\",80\r\n");

	/* Verify successful connection response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XCONNECT: 1,1") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Send data via AT#XSEND with unformatted string
 * - Command: AT#XSEND=<handle>,<mode>,<flags>,"<data>"\r\n
 * - Tests: Sending unformatted string data over TCP socket
 */
void test_xsend_unformatted_string(void)
{
	const char *response;
	const char *test_data = "Hello World";
	int test_data_len = strlen(test_data);

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock successful send - send all data in one call */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_send_ExpectAndReturn(1, NULL, test_data_len, 0, test_data_len);
	__cmock_nrf_send_IgnoreArg_buffer();

	/* Execute send command: handle=1, mode=0 (unformatted), flags=0 */
	send_at_command("AT#XSEND=1,0,0,\"Hello World\"\r\n");

	/* Verify response shows bytes sent */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSEND:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,0,11") != NULL); /* handle=1, result=0, sent=11 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Send data via AT#XSEND with hex string
 * - Command: AT#XSEND=<handle>,<mode>,<flags>,"<hex_data>"\r\n
 * - Tests: Sending hex-encoded data over TCP socket (mode=1)
 */
void test_xsend_hex_string(void)
{
	const char *response;
	/* "48656C6C6F" is hex for "Hello" - 5 bytes when converted from hex */
	int binary_len = 5;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock successful send - send all data in one call */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_send_ExpectAndReturn(4, NULL, binary_len, 0, binary_len);
	__cmock_nrf_send_IgnoreArg_buffer();

	/* Execute send command: handle=4, mode=1 (hex), flags=0 */
	send_at_command("AT#XSEND=4,1,0,\"48656C6C6F\"\r\n");

	/* Verify response shows bytes sent */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSEND:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "4,0,5") != NULL); /* handle=4, result=0, sent=5 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Send data via AT#XSEND with acknowledgment flag
 * - Command: AT#XSEND=<handle>,<mode>,<flags>,"<data>"\r\n
 * - Tests: Sending data with SM_MSG_SEND_ACK flag (0x2000=8192) for network acknowledgment
 */
void test_xsend_with_ack_flag(void)
{
	const char *response;
	const char *test_data = "Test";
	int test_data_len = strlen(test_data);

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock successful send with ACK flag (0x2000 = 8192) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set send callback */
	/* Note: flags will be 0 after SM_MSG_SEND_ACK (0x2000=8192 not 512!) is stripped */
	__cmock_nrf_send_ExpectAnyArgsAndReturn(test_data_len);

	/* Execute send command: handle=4, mode=0 (unformatted), flags=8192 (0x2000,
	 * SM_MSG_SEND_ACK)
	 */
	send_at_command("AT#XSEND=4,0,8192,\"Test\"\r\n");

	/* Verify response shows bytes sent with ACK result type (1) */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSEND:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "4,1,4") != NULL); /* handle=4, result=1 (ACK), sent=4 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/* Helper callback for mocking partial nrf_send */
static ssize_t mock_nrf_send_partial_callback(int socket, const void *buffer, size_t length,
					       int flags, int num_calls)
{
	/* First call: send 5 bytes out of 13 */
	if (num_calls == 0) {
		return 5;
	}
	/* Second call: send remaining 8 bytes */
	return 8;
}

/*
 * Test: Send data via AT#XSEND with partial send
 * - Tests: Handling when nrf_send() sends less data than requested
 */
void test_xsend_partial_send(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock partial send: first call sends 5 bytes, second call sends remaining 8 bytes */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_send_Stub(mock_nrf_send_partial_callback);

	/* Execute send command: handle=0, mode=0 (unformatted), flags=0 */
	send_at_command("AT#XSEND=0,0,0,\"HelloWorld123\"\r\n");

	/* Verify response shows all bytes sent (13) */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSEND:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "0,0,13") != NULL); /* handle=0, result=0, sent=13 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/* Helper callback for mocking failed nrf_send with errno */
static ssize_t mock_nrf_send_error_callback(int socket, const void *buffer, size_t length,
					    int flags, int num_calls)
{
	/* Set errno to ENOTCONN */
	errno = ENOTCONN;
	return -1;
}

/*
 * Test: Send fails with error
 * - Tests: Error handling when nrf_send() fails
 */
void test_xsend_error(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock failed send with ENOTCONN error via callback */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_send_Stub(mock_nrf_send_error_callback);

	/* Execute send command: should fail */
	send_at_command("AT#XSEND=4,0,0,\"Test\"\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Send data via AT#XSEND in data mode
 * - Command: AT#XSEND=<handle>,2,<flags>,<data_len>\r\n followed by raw data
 * - Tests: Sending data using AT_SOCKET_MODE_DATA (mode 2)
 */
void test_xsend_data_mode(void)
{
	const char *response;
	const char *test_data = "Hello World";
	bool stop_at_receive = false;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Enter data mode: socket 1, mode 2 (data), flags 0, length 11 */
	send_at_command("AT#XSEND=1,2,0,11\r\n");

	/* Verify OK response for entering data mode */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	clear_captured_response();

	/* Send data in data mode - this invokes socket_datamode_callback(DATAMODE_SEND) */
	/* do_send calls clear_so_send_cb (or set if flags have ack) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear/set send callback */
	__cmock_nrf_send_ExpectAndReturn(1, test_data, 11, 0, 11);
	sm_at_receive((const uint8_t *)test_data, 11, &stop_at_receive);

	/* Exit data mode with termination pattern */
	send_at_command("+++");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XDATAMODE: 0") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Send data via AT#XSEND in data mode with partial quit string
 * - Command: AT#XSEND=<handle>,2,<flags>\r\n followed by raw data and quit string
 * - Tests: Sending data that includes partial quit string ("++") followed by more data
 * - Expected: Partial quit string is treated as data, full quit string ("+++") exits data mode
 */
void test_xsend_data_mode_partial_quit_string(void)
{
	const char *response;
	const char *test_data = "Hello++World";
	bool stop_at_receive = false;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Enter data mode: socket 1, mode 2 (data), flags 0, no data_len specified */
	send_at_command("AT#XSEND=1,2,0\r\n");

	/* Verify OK response for entering data mode */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	clear_captured_response();

	/* Send data in data mode with partial quit string (++)
	 * The default quit string is "+++" so "++" should be treated as data
	 * This invokes socket_datamode_callback(DATAMODE_SEND)
	 */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear/set send callback */
	__cmock_nrf_send_ExpectAndReturn(1, test_data, 12, 0, 12);
	sm_at_receive((const uint8_t *)test_data, 12, &stop_at_receive);

	/* Exit data mode with full quit string (+++) */
	send_at_command("+++");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XDATAMODE: 0") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Send data via AT#XSENDTO with unformatted string
 * - Command: AT#XSENDTO=<handle>,<mode>,<flags>,"<url>",<port>,"<data>"\r\n
 * - Tests: Sending unformatted string data over UDP socket
 */
void test_xsendto_unformatted_string(void)
{
	const char *response;
	const char *test_data = "Hello UDP";
	int test_data_len = strlen(test_data);

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock DNS resolution via getaddrinfo */
	__cmock_zsock_getaddrinfo_Stub(mock_getaddrinfo_success_callback);
	__cmock_zsock_freeaddrinfo_Expect(NULL);
	__cmock_zsock_freeaddrinfo_IgnoreArg_ai();

	/* Mock successful sendto - send all data in one call */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_sendto_ExpectAndReturn(4, NULL, test_data_len, 0, NULL,
					   sizeof(struct nrf_sockaddr_in), test_data_len);
	__cmock_nrf_sendto_IgnoreArg_message();
	__cmock_nrf_sendto_IgnoreArg_dest_addr();

	/* Execute sendto command: handle=4, mode=0 (unformatted), flags=0 */
	send_at_command("AT#XSENDTO=4,0,0,\"192.168.1.1\",5000,\"Hello UDP\"\r\n");

	/* Verify response shows bytes sent */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSENDTO:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "4,0,9") != NULL); /* handle=4, result=0, sent=9 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Send data via AT#XSENDTO with hex string
 * - Command: AT#XSENDTO=<handle>,<mode>,<flags>,"<url>",<port>,"<hex_data>"\r\n
 * - Tests: Sending hex-encoded data over UDP socket (mode=1)
 */
void test_xsendto_hex_string(void)
{
	const char *response;
	/* "48656C6C6F" is hex for "Hello" - 5 bytes when converted from hex */
	int binary_len = 5;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock DNS resolution via getaddrinfo */
	__cmock_zsock_getaddrinfo_Stub(mock_getaddrinfo_success_callback);
	__cmock_zsock_freeaddrinfo_Expect(NULL);
	__cmock_zsock_freeaddrinfo_IgnoreArg_ai();

	/* Mock successful sendto - send all data in one call */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_sendto_ExpectAndReturn(3, NULL, binary_len, 0, NULL,
					   sizeof(struct nrf_sockaddr_in), binary_len);
	__cmock_nrf_sendto_IgnoreArg_message();
	__cmock_nrf_sendto_IgnoreArg_dest_addr();

	/* Execute sendto command: handle=3, mode=1 (hex), flags=0 */
	send_at_command("AT#XSENDTO=3,1,0,\"192.168.1.1\",5000,\"48656C6C6F\"\r\n");

	/* Verify response shows bytes sent */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSENDTO:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "3,0,5") != NULL); /* handle=3, result=0, sent=5 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: Send data via AT#XSENDTO with acknowledgment flag
 * - Command: AT#XSENDTO=<handle>,<mode>,<flags>,"<url>",<port>,"<data>"\r\n
 * - Tests: Sending data with SM_MSG_SEND_ACK flag (0x2000=8192) for network acknowledgment
 */
void test_xsendto_with_ack_flag(void)
{
	const char *response;
	const char *test_data = "Test";
	int test_data_len = strlen(test_data);

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock DNS resolution via getaddrinfo */
	__cmock_zsock_getaddrinfo_Stub(mock_getaddrinfo_success_callback);
	__cmock_zsock_freeaddrinfo_Expect(NULL);
	__cmock_zsock_freeaddrinfo_IgnoreArg_ai();

	/* Mock successful sendto with ACK flag (0x2000 = 8192) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set send callback */
	/* Note: flags will be 0 after SM_MSG_SEND_ACK (0x2000=8192) is stripped */
	__cmock_nrf_sendto_ExpectAnyArgsAndReturn(test_data_len);

	/* Execute sendto command: handle=1, mode=0 (unformatted), flags=8192 (0x2000,
	 * SM_MSG_SEND_ACK)
	 */
	send_at_command("AT#XSENDTO=1,0,8192,\"192.168.1.1\",5000,\"Test\"\r\n");

	/* Verify response shows bytes sent with ACK result type (1) */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSENDTO:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,1,4") !=
			 NULL); /* handle=1, result=1 (ACK), sent=4 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/* Helper callback for mocking failed nrf_sendto with errno */
static ssize_t mock_nrf_sendto_error_callback(int socket, const void *message, size_t length,
					      int flags, const struct nrf_sockaddr *dest_addr,
					      nrf_socklen_t dest_len, int num_calls)
{
	/* Set errno to ENETUNREACH */
	errno = ENETUNREACH;
	return -1;
}

/*
 * Test: Sendto fails with error
 * - Tests: Error handling when nrf_sendto() fails
 */
void test_xsendto_error(void)
{
	const char *response;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock DNS resolution via getaddrinfo */
	__cmock_zsock_getaddrinfo_Stub(mock_getaddrinfo_success_callback);
	__cmock_zsock_freeaddrinfo_Expect(NULL);
	__cmock_zsock_freeaddrinfo_IgnoreArg_ai();

	/* Mock failed sendto with ENETUNREACH error via callback */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_sendto_Stub(mock_nrf_sendto_error_callback);

	/* Execute sendto command: should fail */
	send_at_command("AT#XSENDTO=3,0,0,\"192.168.1.1\",5000,\"Test\"\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: Sendto with IPv6 address
 * - Command: AT#XSENDTO=<handle>,<mode>,<flags>,"<ipv6_url>",<port>,"<data>"\r\n
 * - Tests: Sending data to IPv6 address
 */
void test_xsendto_ipv6(void)
{
	const char *response;
	const char *test_data = "IPv6 Test";
	int test_data_len = strlen(test_data);

	/* Create IPv6 UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=2,2,0\r\n");
	clear_captured_response();

	/* Mock DNS resolution via getaddrinfo */
	__cmock_zsock_getaddrinfo_Stub(mock_getaddrinfo_success_callback);
	__cmock_zsock_freeaddrinfo_Expect(NULL);
	__cmock_zsock_freeaddrinfo_IgnoreArg_ai();

	/* Mock successful sendto - send all data in one call */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear send callback */
	__cmock_nrf_sendto_ExpectAndReturn(4, NULL, test_data_len, 0, NULL,
					   sizeof(struct nrf_sockaddr_in6), test_data_len);
	__cmock_nrf_sendto_IgnoreArg_message();
	__cmock_nrf_sendto_IgnoreArg_dest_addr();
	__cmock_nrf_sendto_IgnoreArg_dest_len();

	/* Execute sendto command: handle=4, mode=0 (unformatted), flags=0 */
	send_at_command("AT#XSENDTO=4,0,0,\"2001:db8::1\",5000,\"IPv6 Test\"\r\n");

	/* Verify response shows bytes sent */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSENDTO:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "4,0,9") != NULL); /* handle=4, result=0, sent=9 */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Send data via AT#XSENDTO in data mode
 * - Command: AT#XSENDTO=<handle>,2,<flags>,"<url>",<port>,<data_len>\r\n followed by raw data
 * - Tests: Sending data using AT_SOCKET_MODE_DATA (mode 2) with destination
 */
void test_xsendto_data_mode(void)
{
	const char *response;
	const char *test_data = "Hello World";
	bool stop_at_receive = false;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Enter data mode: socket 2, mode 2 (data), flags 0, host, port, length 11 */
	send_at_command("AT#XSENDTO=2,2,0,\"example.com\",8080,11\r\n");

	/* Verify OK response for entering data mode */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	clear_captured_response();

	/* Send data in data mode - this invokes socket_datamode_callback(DATAMODE_SEND) */
	/* do_sendto calls clear_so_send_cb (or set if flags have ack) and DNS resolution */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Clear/set send callback */
	__cmock_zsock_getaddrinfo_Stub(mock_getaddrinfo_success_callback);
	__cmock_nrf_sendto_ExpectAnyArgsAndReturn(11);
	__cmock_zsock_freeaddrinfo_ExpectAnyArgs();
	sm_at_receive((const uint8_t *)test_data, 11, &stop_at_receive);

	/* Exit data mode with termination pattern */
	send_at_command("+++");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XDATAMODE: 0") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

/* Helper callback for mocking nrf_recv with data */
static ssize_t mock_nrf_recv_callback(int socket, void *buffer, size_t length, int flags,
				      int num_calls)
{
	const char *test_data = "Hello from recv";
	size_t data_len = strlen(test_data);

	if (length >= data_len) {
		memcpy(buffer, test_data, data_len);
		return data_len;
	}
	return -1;
}

/*
 * Test: Receive data via AT#XRECV with unformatted mode
 * - Command: AT#XRECV=<handle>,<mode>,<flags>,<timeout>\r\n
 * - Tests: Receiving unformatted string data over TCP socket
 */
void test_xrecv_unformatted_string(void)
{
	const char *response;

	/* Create TCP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock successful recv - receive data */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recv_Stub(mock_nrf_recv_callback);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll event update */

	/* Execute recv command: handle=0, mode=0 (unformatted), flags=0, timeout=5 */
	send_at_command("AT#XRECV=0,0,0,5\r\n");

	/* Verify response shows bytes received */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECV:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "0,0,15") != NULL); /* handle=0, mode=0, received=15 */
	TEST_ASSERT_TRUE(strstr(response, "Hello from recv") != NULL); /* Data in response */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/* Helper callback for mocking nrf_recv with hex data */
static ssize_t mock_nrf_recv_hex_callback(int socket, void *buffer, size_t length, int flags,
					  int num_calls)
{
	/* Return binary data: 0x48 0x65 0x6C 0x6C 0x6F = "Hello" */
	const uint8_t test_data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
	size_t data_len = sizeof(test_data);

	if (length >= data_len) {
		memcpy(buffer, test_data, data_len);
		return data_len;
	}
	return -1;
}

/*
 * Test: Receive data via AT#XRECV with hex mode
 * - Command: AT#XRECV=<handle>,<mode>,<flags>,<timeout>\r\n
 * - Tests: Receiving hex-encoded data over TCP socket (mode=1)
 */
void test_xrecv_hex_string(void)
{
	const char *response;

	/* Create TCP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock successful recv - receive binary data */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recv_Stub(mock_nrf_recv_hex_callback);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll event update */

	/* Execute recv command: handle=1, mode=1 (hex), flags=0, timeout=5 */
	send_at_command("AT#XRECV=1,1,0,5\r\n");

	/* Verify response shows bytes received in hex format */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECV:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,1,5") != NULL); /* handle=1, mode=1, received=5 */
	TEST_ASSERT_TRUE(strstr(response, "48656C6C6F") !=
			 NULL); /* Hex data in response (uppercase) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/* Helper callback for mocking nrf_recv with limited data */
static ssize_t mock_nrf_recv_limited_callback(int socket, void *buffer, size_t length, int flags,
					      int num_calls)
{
	/* Return only 10 bytes even though more could be received */
	const char *test_data = "0123456789";
	size_t data_len = 10;

	if (length >= data_len) {
		memcpy(buffer, test_data, data_len);
		return data_len;
	}
	return -1;
}

/*
 * Test: Receive data via AT#XRECV with specific data length
 * - Command: AT#XRECV=<handle>,<mode>,<flags>,<timeout>,<data_len>\r\n
 * - Tests: Receiving specified amount of data
 */
void test_xrecv_with_data_len(void)
{
	const char *response;

	/* Create TCP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock successful recv - receive limited data */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recv_Stub(mock_nrf_recv_limited_callback);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll event update */

	/* Execute recv command: handle=3, mode=0, flags=0, timeout=5, data_len=10 */
	send_at_command("AT#XRECV=3,0,0,5,10\r\n");

	/* Verify response shows bytes received */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECV:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "3,0,10") != NULL); /* handle=3, mode=0, received=10 */
	TEST_ASSERT_TRUE(strstr(response, "0123456789") != NULL); /* Data in response */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/* Helper callback for mocking failed nrf_recv with errno */
static ssize_t mock_nrf_recv_error_callback(int socket, void *buffer, size_t length, int flags,
					    int num_calls)
{
	/* Set errno to EAGAIN (timeout) */
	errno = EAGAIN;
	return -1;
}

/*
 * Test: Recv fails with error (timeout)
 * - Tests: Error handling when nrf_recv() fails
 */
void test_xrecv_timeout(void)
{
	const char *response;

	/* Create TCP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock failed recv with EAGAIN error via callback */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recv_Stub(mock_nrf_recv_error_callback);
	/* No poll event update mock needed since recv fails */

	/* Execute recv command: should timeout */
	send_at_command("AT#XRECV=2,0,0,1\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

/* Helper callback for mocking nrf_recv returning zero (connection closed) */
static ssize_t mock_nrf_recv_zero_callback(int socket, void *buffer, size_t length, int flags,
					   int num_calls)
{
	/* Return 0 to indicate connection closed */
	return 0;
}

/*
 * Test: Recv returns zero (connection closed)
 * - Tests: Handling when nrf_recv() returns 0 (peer closed connection)
 */
void test_xrecv_connection_closed(void)
{
	const char *response;

	/* Create TCP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Mock recv returning 0 (connection closed) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recv_Stub(mock_nrf_recv_zero_callback);

	/* Execute recv command */
	send_at_command("AT#XRECV=2,0,0,5\r\n");

	/* Verify OK response (recv of 0 is treated as success) */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* No #XRECV response when recv returns 0 */
	TEST_ASSERT_TRUE(strstr(response, "#XRECV:") == NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

/* Helper callback for mocking zsock_inet_ntop */
static char *mock_zsock_inet_ntop_callback(sa_family_t af, const void *src, char *dst,
					   socklen_t size, int num_calls)
{
	if (af == AF_INET) {
		strcpy(dst, "192.168.0.1");
	} else if (af == AF_INET6) {
		strcpy(dst, "2001:db8::1");
	}
	return dst;
}

/* Helper callback for mocking zsock_inet_ntop for specific IPs */
static char *mock_zsock_inet_ntop_10_0_0_1_callback(sa_family_t af, const void *src, char *dst,
						    socklen_t size, int num_calls)
{
	strcpy(dst, "10.0.0.1");
	return dst;
}

static char *mock_zsock_inet_ntop_192_168_0_100_callback(sa_family_t af, const void *src, char *dst,
							 socklen_t size, int num_calls)
{
	strcpy(dst, "192.168.0.100");
	return dst;
}

/* Helper callback for mocking nrf_recvfrom with data and address */
static ssize_t mock_nrf_recvfrom_callback(int socket, void *buffer, size_t length, int flags,
					  struct nrf_sockaddr *address, nrf_socklen_t *address_len,
					  int num_calls)
{
	const char *test_data = "UDP data";
	size_t data_len = strlen(test_data);
	struct sockaddr_in *sa_in = (struct sockaddr_in *)address;

	if (length >= data_len) {
		memcpy(buffer, test_data, data_len);
		/* Set up source address */
		sa_in->sin_family = AF_INET;
		sa_in->sin_port = htons(8080);
		sa_in->sin_addr.s_addr = htonl(0xC0A80001); /* 192.168.0.1 */
		*address_len = sizeof(struct sockaddr_in);
		return data_len;
	}
	return -1;
}

/*
 * Test: Receive data via AT#XRECVFROM with unformatted mode
 * - Command: AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>\r\n
 * - Tests: Receiving unformatted string data over UDP socket with source address
 */
void test_xrecvfrom_unformatted_string(void)
{
	const char *response;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock successful recvfrom - receive data with source address */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recvfrom_Stub(mock_nrf_recvfrom_callback);
	__cmock_zsock_inet_ntop_Stub(mock_zsock_inet_ntop_callback);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll event update */

	/* Execute recvfrom command: handle=1, mode=0 (unformatted), flags=0, timeout=5 */
	send_at_command("AT#XRECVFROM=1,0,0,5\r\n");

	/* Verify response shows bytes received with source address */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECVFROM:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,0,8") != NULL); /* handle=1, mode=0, received=8 */
	TEST_ASSERT_TRUE(strstr(response, "192.168.0.1") != NULL); /* Source IP */
	TEST_ASSERT_TRUE(strstr(response, "8080") != NULL); /* Source port */
	TEST_ASSERT_TRUE(strstr(response, "UDP data") != NULL); /* Data in response */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/* Helper callback for mocking nrf_recvfrom with hex data */
static ssize_t mock_nrf_recvfrom_hex_callback(int socket, void *buffer, size_t length, int flags,
					      struct nrf_sockaddr *address,
					      nrf_socklen_t *address_len, int num_calls)
{
	/* Return binary data: 0x48 0x65 0x6C 0x6C 0x6F = "Hello" */
	const uint8_t test_data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
	size_t data_len = sizeof(test_data);
	struct sockaddr_in *sa_in = (struct sockaddr_in *)address;

	if (length >= data_len) {
		memcpy(buffer, test_data, data_len);
		/* Set up source address */
		sa_in->sin_family = AF_INET;
		sa_in->sin_port = htons(9000);
		sa_in->sin_addr.s_addr = htonl(0x0A000001); /* 10.0.0.1 */
		*address_len = sizeof(struct sockaddr_in);
		return data_len;
	}
	return -1;
}

/*
 * Test: Receive data via AT#XRECVFROM with hex mode
 * - Command: AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>\r\n
 * - Tests: Receiving hex-encoded data over UDP socket (mode=1)
 */
void test_xrecvfrom_hex_string(void)
{
	const char *response;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock successful recvfrom - receive binary data */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recvfrom_Stub(mock_nrf_recvfrom_hex_callback);
	__cmock_zsock_inet_ntop_Stub(mock_zsock_inet_ntop_10_0_0_1_callback);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll event update */

	/* Execute recvfrom command: handle=1, mode=1 (hex), flags=0, timeout=5 */
	send_at_command("AT#XRECVFROM=1,1,0,5\r\n");

	/* Verify response shows bytes received in hex format with source address */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECVFROM:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1,1,5") != NULL); /* handle=1, mode=1, received=5 */
	TEST_ASSERT_TRUE(strstr(response, "10.0.0.1") != NULL); /* Source IP */
	TEST_ASSERT_TRUE(strstr(response, "9000") != NULL); /* Source port */
	TEST_ASSERT_TRUE(strstr(response, "48656C6C6F") != NULL); /* Hex data (uppercase) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/* Helper callback for mocking nrf_recvfrom with IPv6 address */
static ssize_t mock_nrf_recvfrom_ipv6_callback(int socket, void *buffer, size_t length, int flags,
					       struct nrf_sockaddr *address,
					       nrf_socklen_t *address_len, int num_calls)
{
	const char *test_data = "IPv6 UDP";
	size_t data_len = strlen(test_data);
	struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6 *)address;

	if (length >= data_len) {
		memcpy(buffer, test_data, data_len);
		/* Set up IPv6 source address */
		sa_in6->sin6_family = AF_INET6;
		sa_in6->sin6_port = htons(7000);
		/* 2001:db8::1 */
		*address_len = sizeof(struct sockaddr_in6);
		return data_len;
	}
	return -1;
}

/*
 * Test: Receive data via AT#XRECVFROM with IPv6 source address
 * - Command: AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>\r\n
 * - Tests: Receiving data from IPv6 source
 */
void test_xrecvfrom_ipv6(void)
{
	const char *response;

	/* Create IPv6 UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=2,2,0\r\n");
	clear_captured_response();

	/* Mock successful recvfrom - receive data with IPv6 source address */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recvfrom_Stub(mock_nrf_recvfrom_ipv6_callback);
	__cmock_zsock_inet_ntop_Stub(mock_zsock_inet_ntop_callback);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll event update */

	/* Execute recvfrom command: handle=2, mode=0, flags=0, timeout=5 */
	send_at_command("AT#XRECVFROM=2,0,0,5\r\n");

	/* Verify response shows bytes received with IPv6 source address */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECVFROM:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "2,0,8") != NULL); /* handle=2, mode=0, received=8 */
	TEST_ASSERT_TRUE(strstr(response, "2001:db8::1") != NULL); /* Source IPv6 */
	TEST_ASSERT_TRUE(strstr(response, "7000") != NULL); /* Source port */
	TEST_ASSERT_TRUE(strstr(response, "IPv6 UDP") != NULL); /* Data in response */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

/* Helper callback for mocking nrf_recvfrom with limited data */
static ssize_t mock_nrf_recvfrom_limited_callback(int socket, void *buffer, size_t length,
						   int flags, struct nrf_sockaddr *address,
						   nrf_socklen_t *address_len, int num_calls)
{
	/* Return only 10 bytes */
	const char *test_data = "0123456789";
	size_t data_len = 10;
	struct sockaddr_in *sa_in = (struct sockaddr_in *)address;

	if (length >= data_len) {
		memcpy(buffer, test_data, data_len);
		/* Set up source address */
		sa_in->sin_family = AF_INET;
		sa_in->sin_port = htons(5000);
		sa_in->sin_addr.s_addr = htonl(0xC0A80064); /* 192.168.0.100 */
		*address_len = sizeof(struct sockaddr_in);
		return data_len;
	}
	return -1;
}

/*
 * Test: Receive data via AT#XRECVFROM with specific data length
 * - Command: AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>,<data_len>\r\n
 * - Tests: Receiving specified amount of data
 */
void test_xrecvfrom_with_data_len(void)
{
	const char *response;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock successful recvfrom - receive limited data */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recvfrom_Stub(mock_nrf_recvfrom_limited_callback);
	__cmock_zsock_inet_ntop_Stub(mock_zsock_inet_ntop_192_168_0_100_callback);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Poll event update */

	/* Execute recvfrom command: handle=0, mode=0, flags=0, timeout=5, data_len=10 */
	send_at_command("AT#XRECVFROM=0,0,0,5,10\r\n");

	/* Verify response shows bytes received */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECVFROM:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "0,0,10") != NULL); /* handle=0, mode=0, received=10 */
	TEST_ASSERT_TRUE(strstr(response, "192.168.0.100") != NULL); /* Source IP */
	TEST_ASSERT_TRUE(strstr(response, "5000") != NULL); /* Source port */
	TEST_ASSERT_TRUE(strstr(response, "0123456789") != NULL); /* Data in response */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/* Helper callback for mocking failed nrf_recvfrom with errno */
static ssize_t mock_nrf_recvfrom_error_callback(int socket, void *buffer, size_t length, int flags,
						struct nrf_sockaddr *address,
						nrf_socklen_t *address_len, int num_calls)
{
	/* Set errno to EAGAIN (timeout) */
	errno = EAGAIN;
	return -1;
}

/*
 * Test: Recvfrom fails with error (timeout)
 * - Tests: Error handling when nrf_recvfrom() fails
 */
void test_xrecvfrom_timeout(void)
{
	const char *response;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock failed recvfrom with EAGAIN error via callback */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recvfrom_Stub(mock_nrf_recvfrom_error_callback);
	/* No poll event update mock needed since recvfrom fails */

	/* Execute recvfrom command: should timeout */
	send_at_command("AT#XRECVFROM=0,0,0,1\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/* Helper callback for mocking nrf_recvfrom returning zero (datagram received) */
static ssize_t mock_nrf_recvfrom_zero_callback(int socket, void *buffer, size_t length, int flags,
					       struct nrf_sockaddr *address,
					       nrf_socklen_t *address_len, int num_calls)
{
	/* Return 0 to indicate zero-length datagram received */
	struct sockaddr_in *sa_in = (struct sockaddr_in *)address;

	sa_in->sin_family = AF_INET;
	sa_in->sin_port = htons(3000);
	sa_in->sin_addr.s_addr = htonl(0xC0A80002); /* 192.168.0.2 */
	*address_len = sizeof(struct sockaddr_in);
	return 0;
}

/*
 * Test: Recvfrom returns zero (zero-length datagram)
 * - Tests: Handling when nrf_recvfrom() returns 0 (zero-length datagram)
 */
void test_xrecvfrom_zero_length(void)
{
	const char *response;

	/* Create UDP socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	clear_captured_response();

	/* Mock recvfrom returning 0 (zero-length datagram) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Set receive timeout */
	__cmock_nrf_recvfrom_Stub(mock_nrf_recvfrom_zero_callback);

	/* Execute recvfrom command */
	send_at_command("AT#XRECVFROM=3,0,0,5\r\n");

	/* Verify OK response (recvfrom of 0 is treated as success for UDP) */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* No #XRECVFROM response when recvfrom returns 0 */
	TEST_ASSERT_TRUE(strstr(response, "#XRECVFROM:") == NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: Enable async polling on a specific socket for POLLIN events
 * - Command: AT#XAPOLL=<handle>,1,<events>\r\n
 * - Tests: Starting async polling for read events
 */
void test_xapoll_start_pollin(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB - initial setup */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Start async polling for POLLIN (value 1) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB - update for xapoll */
	send_at_command("AT#XAPOLL=0,1,1\r\n");

	/* Verify OK response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/*
 * Test: Enable async polling on a specific socket for POLLOUT events
 * - Command: AT#XAPOLL=<handle>,1,<events>\r\n
 * - Tests: Starting async polling for write events
 */
void test_xapoll_start_pollout(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Start async polling for POLLOUT (value 4) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Update poll events */
	send_at_command("AT#XAPOLL=2,1,4\r\n");

	/* Verify OK response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

/*
 * Test: Enable async polling for both POLLIN and POLLOUT events
 * - Command: AT#XAPOLL=<handle>,1,<events>\r\n
 * - Tests: Starting async polling for read and write events (5 = 1|4)
 */
void test_xapoll_start_pollin_pollout(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Start async polling for POLLIN | POLLOUT (value 5) */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Update poll events */
	send_at_command("AT#XAPOLL=2,1,5\r\n");

	/* Verify OK response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

/*
 * Test: Stop async polling on a specific socket
 * - Command: AT#XAPOLL=<handle>,0\r\n
 * - Tests: Stopping async polling for a socket
 */
void test_xapoll_stop_socket(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Start async polling first */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Update poll events */
	send_at_command("AT#XAPOLL=4,1,1\r\n");
	clear_captured_response();

	/* Stop async polling */
	send_at_command("AT#XAPOLL=4,0\r\n");

	/* Verify OK response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Start async polling on all sockets
 * - Command: AT#XAPOLL=,1,<events>\r\n (no handle specified)
 * - Tests: Starting async polling for all open sockets
 */
void test_xapoll_start_all_sockets(void)
{
	const char *response;
	int max_sockets = CONFIG_POSIX_OPEN_MAX - 1;

	/* Create multiple sockets */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP,
						   i);
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
		send_at_command("AT#XSOCKET=1,1,0\r\n");
		clear_captured_response();
	}

	/* Start async polling for all sockets (no handle specified) */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Update poll events */
	}
	send_at_command("AT#XAPOLL=,1,1\r\n");

	/* Verify OK response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close all sockets */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_close_ExpectAndReturn(i, 0);
	}
	send_at_command("AT#XCLOSE=0\r\n");
	send_at_command("AT#XCLOSE=1\r\n");
	send_at_command("AT#XCLOSE=2\r\n");
}

/*
 * Test: Stop async polling on all sockets
 * - Command: AT#XAPOLL=,0\r\n (no handle specified)
 * - Tests: Stopping async polling for all open sockets
 */
void test_xapoll_stop_all_sockets(void)
{
	const char *response;
	int max_sockets = CONFIG_POSIX_OPEN_MAX - 1;

	/* Create multiple sockets */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP,
						   i);
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
		send_at_command("AT#XSOCKET=1,1,0\r\n");
		clear_captured_response();
	}

	/* Start async polling for all sockets first */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Update poll events */
	}
	send_at_command("AT#XAPOLL=,1,1\r\n");
	clear_captured_response();

	/* Stop async polling for all sockets */
	send_at_command("AT#XAPOLL=,0\r\n");

	/* Verify OK response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close all sockets */
	for (int i = 0; i < max_sockets; i++) {
		__cmock_nrf_close_ExpectAndReturn(i, 0);
	}
	send_at_command("AT#XCLOSE=0\r\n");
	send_at_command("AT#XCLOSE=1\r\n");
	send_at_command("AT#XCLOSE=2\r\n");
}

/*
 * Test: Read async poll status
 * - Command: AT#XAPOLL?\r\n
 * - Tests: Reading which sockets have async polling enabled
 */
void test_xapoll_read(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Start async polling for POLLIN */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Update poll events */
	send_at_command("AT#XAPOLL=2,1,1\r\n");
	clear_captured_response();

	/* Read async poll status */
	send_at_command("AT#XAPOLL?\r\n");

	/* Verify response contains socket handle and events */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XAPOLL:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "2,1") != NULL); /* handle=2, events=1 (POLLIN) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

/*
 * Test: AT#XAPOLL=? (TEST command type)
 * - Verifies TEST command returns syntax help for async polling
 * - Expected response includes operation (START/STOP) and event types (POLLIN/POLLOUT)
 */
void test_xapoll_test_command(void)
{
	const char *response;

	/* Send TEST command */
	send_at_command("AT#XAPOLL=?\r\n");

	/* Verify response contains syntax help */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XAPOLL:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* Verify it contains operation types: 0 (STOP), 1 (START) */
	TEST_ASSERT_TRUE(strstr(response, "0,1") != NULL);
}

/*
 * Test: Async poll with invalid events
 * - Command: AT#XAPOLL=<handle>,1,<invalid_events>\r\n
 * - Tests: Error handling for invalid event flags
 */
void test_xapoll_invalid_events(void)
{
	const char *response;

	/* Create socket first */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	clear_captured_response();

	/* Try to start async polling with invalid events (8 is not valid) */
	send_at_command("AT#XAPOLL=0,1,8\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/*
 * Test: Async poll with invalid socket handle
 * - Command: AT#XAPOLL=<invalid_handle>,1,1\r\n
 * - Tests: Error handling for non-existent socket
 */
void test_xapoll_invalid_socket(void)
{
	const char *response;

	/* Try to start async polling on non-existent socket 999 */
	send_at_command("AT#XAPOLL=999,1,1\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: Close socket via AT command
 * - Command: AT#XCLOSE=<handle>\r\n (close specific socket)
 * - Command: AT#XCLOSE\r\n (close all open sockets)
 * - Tests both individual socket closure and closing all sockets at once
 */
void test_xclose_operation(void)
{
	const char *response;
	int max_sockets = CONFIG_POSIX_OPEN_MAX - 1;

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
	const char *response;

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
}

/*
 * Test: Resolve hostname via AT#XGETADDRINFO command (IPv6)
 * - Command: AT#XGETADDRINFO="hostname",2
 * - Tests: Hostname resolution with IPv6 address family specified
 */
void test_xgetaddrinfo_ipv6(void)
{
	const char *response;

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
}

/*
 * Test: AT#XGETADDRINFO with invalid address family
 * - Command: AT#XGETADDRINFO="hostname",99
 * - Tests: Error handling for invalid address family parameter
 */
void test_xgetaddrinfo_invalid_family(void)
{
	const char *response;

	/* Execute XGETADDRINFO with invalid address family (99) */
	send_at_command("AT#XGETADDRINFO=\"example.com\",99\r\n");

	/* Verify ERROR response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XGETADDRINFO when DNS resolution fails
 * - Command: AT#XGETADDRINFO="invalid.host"
 * - Tests: Error response when hostname cannot be resolved
 */
void test_xgetaddrinfo_dns_failure(void)
{
	const char *response;

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
}

/*
 * Test: Read operation for XSSOCKET listing all open secure sockets
 * - Command: AT#XSSOCKET?\r\n
 * - Tests: Query returns details of all open secure sockets
 */
void test_xssocket_read_operation(void)
{
	const char *response;

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
	send_at_command("AT#XCLOSE=1\r\n");
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: AT#XSSOCKET=? (TEST command type)
 * - Verifies TEST command returns syntax help for secure socket command
 * - Expected response includes family, type, role, sec_tag, peer_verify, cid
 */
void test_xssocket_test_command(void)
{
	const char *response;

	/* Send TEST command */
	send_at_command("AT#XSSOCKET=?\r\n");

	/* Verify response contains syntax help */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* Verify it contains expected values */
	TEST_ASSERT_TRUE(strstr(response, "sec_tag") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "peer_verify") != NULL);
}

/*
 * Test: Create secure IPv4 TCP socket via AT command
 * - Command: AT#XSSOCKET=1,1,0,<sec_tag>\r\n
 * - Tests: IPv4, TCP, Client role, TLS
 */
void test_xssocket_ipv4_tcp_client(void)
{
	const char *response;

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
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: Create secure IPv4 DTLS socket via AT command
 * - Command: AT#XSSOCKET=1,2,0,<sec_tag>\r\n
 * - Tests: IPv4, UDP, Client role, DTLS
 */
void test_xssocket_ipv4_dtls_client(void)
{
	const char *response;

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
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Create secure IPv6 TCP socket via AT command
 * - Command: AT#XSSOCKET=2,1,0,<sec_tag>\r\n
 * - Tests: IPv6, TCP, Client role, TLS
 */
void test_xssocket_ipv6_tcp_client(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 1 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET6, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 1);

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
	TEST_ASSERT_TRUE(strstr(response, "1,1,258") !=
			 NULL); /* handle=1, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: Create secure TLS server socket via AT command
 * - Command: AT#XSSOCKET=1,1,1,<sec_tag>\r\n
 * - Tests: IPv4, TCP, Server role, TLS
 */
void test_xssocket_ipv4_tcp_server(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 0 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 0);

	/* Mock setsockopt calls in the expected order:
	 * 1. SO_SNDTIMEO (SOL_SOCKET)
	 * 2. SO_SEC_TAG_LIST (SOL_SECURE)
	 * 3. SO_SEC_PEER_VERIFY (SOL_SECURE)
	 * 4. SO_SEC_ROLE (SOL_SECURE) - server only, verified below
	 * 5. SO_POLLCB (SOL_SOCKET)
	 */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_TAG_LIST */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SEC_PEER_VERIFY */

	/* Verify SO_SEC_ROLE is set to server (value 1) */
	int expected_role = 1; /* NRF_SO_SEC_ROLE_SERVER */

	__cmock_nrf_setsockopt_ExpectWithArrayAndReturn(
		0,                      /* socket fd */
		NRF_SOL_SECURE,        /* level */
		NRF_SO_SEC_ROLE,       /* option_name */
		&expected_role,        /* option_value */
		1,                     /* option_value array size */
		sizeof(int),           /* option_len */
		0                      /* return value */
	);

	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */

	/* Send AT command: family=1(IPv4), type=1(STREAM), role=1(server), sec_tag=42 */
	send_at_command("AT#XSSOCKET=1,1,1,42\r\n");

	/* Verify response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKET:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "0,1,258") !=
			 NULL); /* handle=0, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(0, 0);
	send_at_command("AT#XCLOSE=0\r\n");
}

/*
 * Test: Create secure socket with custom peer verification
 * - Command: AT#XSSOCKET=1,1,0,<sec_tag>,<peer_verify>\r\n
 * - Tests: Custom peer verification level
 */
void test_xssocket_custom_peer_verify(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 3 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 3);

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
	TEST_ASSERT_TRUE(strstr(response, "3,1,258") !=
			 NULL); /* handle=3, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: Create secure socket with specific PDN context
 * - Command: AT#XSSOCKET=1,1,0,<sec_tag>,<peer_verify>,<cid>\r\n
 * - Tests: Binding to specific PDN context
 */
void test_xssocket_with_pdn_cid(void)
{
	const char *response;

	/* Mock nrf_socket to return fd 1 */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM,
					   258 /* NRF_SPROTO_TLS1v2 */, 1);

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
	TEST_ASSERT_TRUE(strstr(response, "1,1,258") !=
			 NULL); /* handle=1, type=1, proto=258(TLS) */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
}

/*
 * Test: AT#XSOCKETOPT=? (TEST command type)
 * - Verifies TEST command returns syntax help for socket options
 * - Expected response includes operation types (GET, SET)
 */
void test_xsocketopt_test_command(void)
{
	const char *response;

	/* Send TEST command */
	send_at_command("AT#XSOCKETOPT=?\r\n");

	/* Verify response contains syntax help */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKETOPT:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* Verify it contains operation types: 0 (GET), 1 (SET) */
	TEST_ASSERT_TRUE(strstr(response, "0,1") != NULL);
}

/*
 * Test: Set and get socket options for regular sockets
 * - Commands: AT#XSOCKETOPT=<handle>,1,<name>,<value> (set)
 *             AT#XSOCKETOPT=<handle>,0,<name> (get)
 * - Tests: Setting and getting SO_RCVTIMEO and SO_SNDTIMEO options
 */
void test_xsocketopt_set_get(void)
{
	const char *response;

	/* Create a socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 4);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* Bind to PDN */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 4") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set SO_RCVTIMEO (option 20) to 30 seconds */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKETOPT=4,1,20,30\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Set SO_SNDTIMEO (option 21) to 60 seconds */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XSOCKETOPT=4,1,21,60\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Get SO_RCVTIMEO (option 20) - should return 30 */
	__cmock_nrf_getsockopt_Stub(mock_getsockopt_timeval_callback);
	send_at_command("AT#XSOCKETOPT=4,0,20\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKETOPT: 4,30") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Get SO_SNDTIMEO (option 21) - should return 60 */
	send_at_command("AT#XSOCKETOPT=4,0,21\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKETOPT: 4,60") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(4, 0);
	send_at_command("AT#XCLOSE=4\r\n");
}

/*
 * Test: Set socket option SO_REUSEADDR (set-only)
 * - Command: AT#XSOCKETOPT=<handle>,1,2,<value>
 * - Tests: Setting SO_REUSEADDR option (option 2 is set-only)
 */
void test_xsocketopt_reuseaddr(void)
{
	const char *response;

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
	send_at_command("AT#XCLOSE=0\r\n");
}

/*
 * Test: Set and get secure socket options
 * - Commands: AT#XSSOCKETOPT=<handle>,1,<name>,<value> (set)
 *             AT#XSSOCKETOPT=<handle>,0,<name> (get)
 * - Tests: Setting and getting TLS_PEER_VERIFY, TLS_SESSION_CACHE, and TLS_HOSTNAME
 */
void test_xssocketopt_set_get(void)
{
	const char *response;

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
	send_at_command("AT#XCLOSE=0\r\n");
}

/*
 * Test: AT#XSSOCKETOPT=? (TEST command type)
 * - Verifies TEST command returns syntax help for secure socket options
 * - Expected response includes operation types (GET, SET)
 */
void test_xssocketopt_test_command(void)
{
	const char *response;

	/* Send TEST command */
	send_at_command("AT#XSSOCKETOPT=?\r\n");

	/* Verify response contains syntax help */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSSOCKETOPT:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* Verify it contains operation types: 0 (GET), 1 (SET) */
	TEST_ASSERT_TRUE(strstr(response, "0,1") != NULL);
}

/*
 * Test: AT#XRECVCFG=? (TEST command type)
 * - Verifies TEST command returns syntax help for receive configuration
 * - Expected response includes flag options and mode types
 */
void test_xrecvcfg_test_command(void)
{
	const char *response;

	/* Send TEST command */
	send_at_command("AT#XRECVCFG=?\r\n");

	/* Verify response contains syntax help */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECVCFG:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	/* Verify it contains expected flag values: (0,1,2,3) and mode values: (0,1) */
	TEST_ASSERT_TRUE(strstr(response, "(0,1,2,3)") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "(0,1)") != NULL);
}

/*
 * Test: AT#XRECVCFG? (READ command type)
 * - Verifies READ command returns configured receive settings for sockets
 * - Creates socket, configures receive mode, then reads configuration
 */
void test_xrecvcfg_read_command(void)
{
	const char *response;

	/* Create a socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 3);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 3") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Configure receive mode: socket 3, flags=1 (AT_MODE), hex_mode=0 */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* POLLCB update for receive config */
	send_at_command("AT#XRECVCFG=3,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Read configuration */
	send_at_command("AT#XRECVCFG?\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECVCFG: 3,1,0") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close socket */
	__cmock_nrf_close_ExpectAndReturn(3, 0);
	send_at_command("AT#XCLOSE=3\r\n");
}

/*
 * Test: AT#XRECVCFG=,<flags>,<hex> (SET command applied to all sockets)
 * - Verifies SET command can be applied to all sockets by omitting handle
 * - Creates multiple sockets, configures receive mode for all, then verifies
 */
void test_xrecvcfg_set_all_sockets(void)
{
	const char *response;

	/* Create first socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_STREAM, NRF_IPPROTO_TCP, 1);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 1") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Create second socket */
	__cmock_nrf_socket_ExpectAndReturn(NRF_AF_INET, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP, 2);
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_SNDTIMEO */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* SO_POLLCB */
	send_at_command("AT#XSOCKET=1,2,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSOCKET: 2") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Configure receive mode for ALL sockets: flags=1 (AT_MODE), hex_mode=0 */
	/* Omit handle parameter to apply to all sockets */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* POLLCB update for socket 1 */
	__cmock_nrf_setsockopt_ExpectAnyArgsAndReturn(0); /* POLLCB update for socket 2 */
	send_at_command("AT#XRECVCFG=,1,0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Read configuration - both sockets should have the same config */
	send_at_command("AT#XRECVCFG?\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XRECVCFG: 1,1,0") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "#XRECVCFG: 2,1,0") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);

	/* Close sockets */
	__cmock_nrf_close_ExpectAndReturn(1, 0);
	send_at_command("AT#XCLOSE=1\r\n");
	__cmock_nrf_close_ExpectAndReturn(2, 0);
	send_at_command("AT#XCLOSE=2\r\n");
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
