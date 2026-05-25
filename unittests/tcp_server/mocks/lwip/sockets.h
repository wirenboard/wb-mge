#pragma once

/* Minimal mock for lwip/sockets.h used in tcp_server unit tests.
 * Uses system socket types, but redirects socket function calls to mock
 * implementations via #define macros so we don't conflict with libc. */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── LWIP-specific constants not in system headers ─────────────────────── */

#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE    0x10   /* LWIP keepalive idle interval */
#endif

#ifndef TCP_KEEPINTVL
#define TCP_KEEPINTVL   0x11
#endif

#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT     0x12
#endif

#ifndef TCP_NODELAY
#define TCP_NODELAY     0x01
#endif

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT    0x80
#endif

/* ── Mock control variables ─────────────────────────────────────────────── */

#define MOCK_RECV_MAX_VALUES 16
#define MOCK_RECV_DATA_SIZE  1024

extern int  mock_recv_return_values[MOCK_RECV_MAX_VALUES];
extern int  mock_recv_return_count;
extern int  mock_recv_call_count;
extern uint8_t mock_recv_data[MOCK_RECV_DATA_SIZE];
extern int  mock_recv_data_len;

extern int  mock_socket_fd;
extern bool mock_socket_should_fail;
extern bool mock_bind_should_fail;
extern bool mock_listen_should_fail;
extern int  mock_accept_fd;
extern int  mock_accept_call_count;
/* When > 0, mock_accept() returns -1 with mock_accept_errno for the first
 * mock_accept_fail_count calls, then falls through to normal behaviour. */
extern int  mock_accept_fail_count;
extern int  mock_accept_errno;

extern int  mock_close_call_count;
extern int  mock_shutdown_call_count;
extern int  mock_send_call_count;
extern int  mock_setsockopt_call_count;

void mock_lwip_sockets_reset(void);

/* ── Mock function declarations ──────────────────────────────────────────── */

int mock_socket(int domain, int type, int protocol);
int mock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int mock_listen(int sockfd, int backlog);
int mock_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t mock_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t mock_send(int sockfd, const void *buf, size_t len, int flags);
int mock_close(int fd);
int mock_shutdown(int sockfd, int how);
int mock_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
char *mock_inet_ntoa_r(struct in_addr in, char *buf, socklen_t size);

/* ── Redirect tcp_server.c calls to mock implementations ─────────────────── */

#define socket(domain, type, proto)     mock_socket((domain), (type), (proto))
#define bind(fd, addr, len)             mock_bind((fd), (addr), (len))
#define listen(fd, backlog)             mock_listen((fd), (backlog))
#define accept(fd, addr, alen)          mock_accept((fd), (addr), (alen))
#define recv(fd, buf, len, flags)       mock_recv((fd), (buf), (len), (flags))
#define send(fd, buf, len, flags)       mock_send((fd), (buf), (len), (flags))
#define close(fd)                       mock_close((fd))
#define shutdown(fd, how)               mock_shutdown((fd), (how))
#define setsockopt(fd,l,n,v,ol)         mock_setsockopt((fd),(l),(n),(v),(ol))
#define inet_ntoa_r(in, buf, sz)        mock_inet_ntoa_r((in), (buf), (sz))
