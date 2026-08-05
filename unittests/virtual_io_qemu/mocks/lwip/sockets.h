#pragma once

/* Minimal mock for lwip/sockets.h used in the virtual_io_qemu unit tests.
 * Uses system socket types, but redirects socket function calls to mock
 * implementations via #define macros so we don't conflict with libc. Unlike the
 * tcp_server mock this one provides sendto/recvfrom: mock_sendto records every
 * emitted datagram so tests can inspect the wire records the module produced. */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Captured-datagram store ─────────────────────────────────────────────── */

#define MOCK_SENDTO_MAX 256
#define MOCK_REC_LEN    8

extern int      mock_sendto_count;
extern uint8_t  mock_sendto_data[MOCK_SENDTO_MAX][MOCK_REC_LEN];
extern int      mock_sendto_len[MOCK_SENDTO_MAX];

void mock_lwip_reset(void);

/* ── Mock function declarations ──────────────────────────────────────────── */

int  mock_socket(int domain, int type, int protocol);
int  mock_bind(int fd, const struct sockaddr *addr, socklen_t len);
int  mock_setsockopt(int fd, int level, int optname, const void *val, socklen_t len);
int  mock_close(int fd);
ssize_t mock_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t addrlen);
ssize_t mock_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *addrlen);

/* ── Redirect virtual_io_qemu.c calls to mock implementations ────────────── */

#define socket(d,t,p)              mock_socket((d),(t),(p))
#define bind(f,a,l)                mock_bind((f),(a),(l))
#define setsockopt(f,l,n,v,o)      mock_setsockopt((f),(l),(n),(v),(o))
#define close(f)                   mock_close((f))
#define sendto(f,b,l,fl,d,a)       mock_sendto((f),(b),(l),(fl),(d),(a))
#define recvfrom(f,b,l,fl,s,a)     mock_recvfrom((f),(b),(l),(fl),(s),(a))
