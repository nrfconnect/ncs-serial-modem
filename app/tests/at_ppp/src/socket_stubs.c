/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file socket_stubs.c
 *
 * Socket and eventfd stubs used by the sm_ppp.c unit tests.
 *
 * The real Zephyr socket headers are used, so only the syscall implementations
 * (z_impl_zsock_*) and zvfs_poll() need to be provided. No real traffic is
 * passed: zvfs_poll() blocks on a condition variable that is signalled either
 * by eventfd_write() (a PPP start/stop/restart event) or by
 * ppp_stub_signal_socket_error() (simulated link failure).
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/eventfd.h>

#include "ppp_stubs.h"

#define STUB_EVENTFD        100
#define STUB_SOCKET_FD_BASE 200
#define STUB_MAX_SOCKETS    8

unsigned int ppp_stub_socket_fail_nth;
bool ppp_stub_bind_fail;
bool ppp_stub_setsockopt_fail;
int ppp_stub_inet_pton_ret = 1;
unsigned int ppp_stub_socket_calls;
unsigned int ppp_stub_close_calls;

static K_MUTEX_DEFINE(stub_lock);
static K_CONDVAR_DEFINE(stub_cond);

static uint64_t evfd_count;
static bool evfd_created;
static bool sock_err_pending;
static bool poll_fail_pending;
static bool poll_held;
static bool sock_open[STUB_MAX_SOCKETS];

unsigned int ppp_stub_max_poll_events;

void ppp_stub_socket_reset(void)
{
	k_mutex_lock(&stub_lock, K_FOREVER);
	ppp_stub_socket_fail_nth = 0;
	ppp_stub_bind_fail = false;
	ppp_stub_setsockopt_fail = false;
	ppp_stub_inet_pton_ret = 1;
	ppp_stub_socket_calls = 0;
	ppp_stub_close_calls = 0;
	sock_err_pending = false;
	poll_fail_pending = false;
	poll_held = false;
	ppp_stub_max_poll_events = 0;
	memset(sock_open, 0, sizeof(sock_open));
	k_mutex_unlock(&stub_lock);
}

int ppp_stub_open_socket_fd(unsigned int idx)
{
	int fd = -1;

	k_mutex_lock(&stub_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(sock_open); i++) {
		if (sock_open[i] && idx-- == 0) {
			fd = STUB_SOCKET_FD_BASE + (int)i;
			break;
		}
	}
	k_mutex_unlock(&stub_lock);
	return fd;
}

void ppp_stub_hold_poll(void)
{
	k_mutex_lock(&stub_lock, K_FOREVER);
	poll_held = true;
	k_mutex_unlock(&stub_lock);
}

void ppp_stub_release_poll(void)
{
	k_mutex_lock(&stub_lock, K_FOREVER);
	poll_held = false;
	k_condvar_broadcast(&stub_cond);
	k_mutex_unlock(&stub_lock);
}

unsigned int ppp_stub_open_socket_count(void)
{
	unsigned int count = 0;

	k_mutex_lock(&stub_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(sock_open); i++) {
		count += sock_open[i] ? 1 : 0;
	}
	k_mutex_unlock(&stub_lock);
	return count;
}

void ppp_stub_signal_socket_error(void)
{
	k_mutex_lock(&stub_lock, K_FOREVER);
	sock_err_pending = true;
	k_condvar_broadcast(&stub_cond);
	k_mutex_unlock(&stub_lock);
}

void ppp_stub_signal_poll_failure(void)
{
	k_mutex_lock(&stub_lock, K_FOREVER);
	poll_fail_pending = true;
	k_condvar_broadcast(&stub_cond);
	k_mutex_unlock(&stub_lock);
}

/* --- eventfd ------------------------------------------------------------ */

int eventfd(unsigned int initval, int flags)
{
	ARG_UNUSED(flags);

	k_mutex_lock(&stub_lock, K_FOREVER);
	evfd_count = initval;
	evfd_created = true;
	k_mutex_unlock(&stub_lock);
	return STUB_EVENTFD;
}

int eventfd_write(int fd, eventfd_t value)
{
	if (fd != STUB_EVENTFD || !evfd_created) {
		errno = EBADF;
		return -1;
	}

	k_mutex_lock(&stub_lock, K_FOREVER);
	evfd_count += value;
	k_condvar_broadcast(&stub_cond);
	k_mutex_unlock(&stub_lock);
	return 0;
}

int eventfd_read(int fd, eventfd_t *value)
{
	int ret = 0;

	if (fd != STUB_EVENTFD || !evfd_created) {
		errno = EBADF;
		return -1;
	}

	k_mutex_lock(&stub_lock, K_FOREVER);
	if (evfd_count == 0) {
		errno = EAGAIN;
		ret = -1;
	} else {
		*value = evfd_count;
		evfd_count = 0;
	}
	k_mutex_unlock(&stub_lock);
	return ret;
}

/* --- poll --------------------------------------------------------------- */

int z_impl_zvfs_poll(struct zvfs_pollfd *fds, int nfds, int poll_timeout)
{
	const k_timeout_t timeout = (poll_timeout < 0) ? K_FOREVER : K_MSEC(poll_timeout);
	int nevents = 0;

	for (int i = 0; i < nfds; i++) {
		fds[i].revents = 0;
	}

	k_mutex_lock(&stub_lock, K_FOREVER);

	while (true) {
		/* Held by ppp_stub_hold_poll() so that a test can compose a
		 * single poll result out of several independent events.
		 */
		if (poll_held) {
			if (k_condvar_wait(&stub_cond, &stub_lock, timeout) != 0) {
				break;
			}
			continue;
		}

		/* A polling failure can only be reported while PPP is running,
		 * as that is the only time the data sockets are polled.
		 */
		if (poll_fail_pending && nfds > 1) {
			poll_fail_pending = false;
			k_mutex_unlock(&stub_lock);
			errno = EIO;
			return -1;
		}

		/* A socket error can only be reported when the data sockets are
		 * part of the polled set, i.e. while PPP is running.
		 */
		const bool deliver_err = sock_err_pending && (nfds > 1);

		if (evfd_count > 0 || deliver_err) {
			for (int i = 0; i < nfds; i++) {
				if (fds[i].fd == STUB_EVENTFD) {
					if (evfd_count > 0) {
						fds[i].revents = ZSOCK_POLLIN;
						nevents++;
					}
				} else if (deliver_err) {
					fds[i].revents = ZSOCK_POLLERR;
					nevents++;
				}
			}
			if (deliver_err) {
				sock_err_pending = false;
			}
			break;
		}

		if (k_condvar_wait(&stub_cond, &stub_lock, timeout) != 0) {
			break;
		}
	}

	if ((unsigned int)nevents > ppp_stub_max_poll_events) {
		ppp_stub_max_poll_events = (unsigned int)nevents;
	}
	k_mutex_unlock(&stub_lock);
	return nevents;
}

/* --- sockets ------------------------------------------------------------ */

int z_impl_zsock_socket(int family, int type, int proto)
{
	ARG_UNUSED(family);
	ARG_UNUSED(type);
	ARG_UNUSED(proto);

	k_mutex_lock(&stub_lock, K_FOREVER);
	ppp_stub_socket_calls++;

	if (ppp_stub_socket_fail_nth == ppp_stub_socket_calls) {
		k_mutex_unlock(&stub_lock);
		errno = ENOMEM;
		return -1;
	}

	for (size_t i = 0; i < ARRAY_SIZE(sock_open); i++) {
		if (!sock_open[i]) {
			sock_open[i] = true;
			k_mutex_unlock(&stub_lock);
			return STUB_SOCKET_FD_BASE + (int)i;
		}
	}

	k_mutex_unlock(&stub_lock);
	errno = EMFILE;
	return -1;
}

int z_impl_zsock_close(int sock)
{
	const int idx = sock - STUB_SOCKET_FD_BASE;

	k_mutex_lock(&stub_lock, K_FOREVER);
	ppp_stub_close_calls++;

	if (idx < 0 || idx >= (int)ARRAY_SIZE(sock_open) || !sock_open[idx]) {
		k_mutex_unlock(&stub_lock);
		errno = EBADF;
		return -1;
	}

	sock_open[idx] = false;
	k_mutex_unlock(&stub_lock);
	return 0;
}

int z_impl_zsock_bind(int sock, const struct net_sockaddr *addr, net_socklen_t addrlen)
{
	ARG_UNUSED(sock);
	ARG_UNUSED(addr);
	ARG_UNUSED(addrlen);

	if (ppp_stub_bind_fail) {
		errno = EADDRINUSE;
		return -1;
	}
	return 0;
}

int z_impl_zsock_setsockopt(int sock, int level, int optname, const void *optval,
			    net_socklen_t optlen)
{
	ARG_UNUSED(sock);
	ARG_UNUSED(level);
	ARG_UNUSED(optname);
	ARG_UNUSED(optval);
	ARG_UNUSED(optlen);

	if (ppp_stub_setsockopt_fail) {
		errno = ENOPROTOOPT;
		return -1;
	}
	return 0;
}

ssize_t z_impl_zsock_recvfrom(int sock, void *buf, size_t max_len, int flags,
			      struct net_sockaddr *src_addr, net_socklen_t *addrlen)
{
	ARG_UNUSED(sock);
	ARG_UNUSED(buf);
	ARG_UNUSED(max_len);
	ARG_UNUSED(flags);
	ARG_UNUSED(src_addr);
	ARG_UNUSED(addrlen);

	errno = EAGAIN;
	return -1;
}

ssize_t z_impl_zsock_sendto(int sock, const void *buf, size_t len, int flags,
			    const struct net_sockaddr *dest_addr, net_socklen_t addrlen)
{
	ARG_UNUSED(sock);
	ARG_UNUSED(buf);
	ARG_UNUSED(flags);
	ARG_UNUSED(dest_addr);
	ARG_UNUSED(addrlen);

	return (ssize_t)len;
}

int z_impl_zsock_inet_pton(net_sa_family_t family, const char *src, void *dst)
{
	ARG_UNUSED(src);

	if (ppp_stub_inet_pton_ret != 1) {
		return ppp_stub_inet_pton_ret;
	}

	/* The tests never inspect the parsed address, only whether parsing
	 * succeeded, so a deterministic pattern is enough.
	 */
	memset(dst, 0xAB, (family == NET_AF_INET6) ? 16 : 4);
	return 1;
}
