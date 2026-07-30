/* Undefine the redirect macros so that this file can call the real system
 * functions or define the mock implementations without infinite recursion. */
#undef socket
#undef bind
#undef listen
#undef accept
#undef recv
#undef send
#undef close
#undef shutdown
#undef setsockopt
#undef inet_ntoa_r

#include "lwip/sockets.h"
#include <string.h>

/* ── Mock state ─────────────────────────────────────────────────────────── */

int  mock_recv_return_values[MOCK_RECV_MAX_VALUES] = {0};
int  mock_recv_return_count = 0;
int  mock_recv_call_count = 0;
uint8_t mock_recv_data[MOCK_RECV_DATA_SIZE] = {0};
int  mock_recv_data_len = 0;
mock_recv_hook_t mock_recv_hook = 0;

int  mock_socket_fd = 5;
bool mock_socket_should_fail = false;
bool mock_bind_should_fail = false;
bool mock_listen_should_fail = false;

/* Arguments of the last socket() call, and the address of the last bind(). The listening
 * socket's family and the exact bytes handed to bind() are what decide whether this server
 * and esp_http_server look like the same listener to lwIP, so they are observable state of
 * the unit under test, not incidental mock bookkeeping — see the tests that assert them. */
int  mock_socket_last_domain = -1;
int  mock_socket_last_type = -1;
int  mock_socket_last_protocol = -1;
int  mock_bind_call_count = 0;
struct sockaddr_storage mock_bind_last_addr;
socklen_t mock_bind_last_addrlen = 0;
int  mock_accept_fd = 10;
int  mock_accept_call_count = 0;
int  mock_accept_fail_count = 0;   /* number of leading ENFILE-class failures */
int  mock_accept_errno = ENFILE;   /* errno to set on simulated failures */

int  mock_close_call_count = 0;
int  mock_shutdown_call_count = 0;
int  mock_send_call_count = 0;
int  mock_setsockopt_call_count = 0;
int  mock_send_last_fd = -1;
mock_close_hook_t mock_close_hook = 0;

/* ── Mock implementations ────────────────────────────────────────────────── */

int mock_socket(int domain, int type, int protocol)
{
    mock_socket_last_domain = domain;
    mock_socket_last_type = type;
    mock_socket_last_protocol = protocol;
    if (mock_socket_should_fail) {
        return -1;
    }
    return mock_socket_fd;
}

int mock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd;

    mock_bind_call_count++;
    mock_bind_last_addrlen = addrlen;
    /* Copy verbatim, including whatever the caller left in the tail — a test that checks
     * the address was fully zeroed can only see uninitialised bytes if the mock does not
     * quietly clean them up on the way in. */
    if (addr) {
        size_t copy_len = (addrlen < (socklen_t)sizeof(mock_bind_last_addr))
                              ? (size_t)addrlen
                              : sizeof(mock_bind_last_addr);
        memcpy(&mock_bind_last_addr, addr, copy_len);
    }

    if (mock_bind_should_fail) {
        return -1;
    }
    return 0;
}

int mock_listen(int sockfd, int backlog)
{
    (void)sockfd;
    (void)backlog;
    if (mock_listen_should_fail) {
        return -1;
    }
    return 0;
}

int mock_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd;
    (void)addrlen;

    /* Zero the FULL sockaddr_storage the acceptor now passes in, not just the sockaddr_in
     * prefix: anything left beyond the prefix is uninitialised caller stack, and the
     * acceptor reads ss_family out of it to decide how to format the client address. */
    if (addr) {
        memset(addr, 0, sizeof(struct sockaddr_storage));
        ((struct sockaddr_in *)addr)->sin_family = AF_INET;
    }

    mock_accept_call_count++;

    /* Simulate leading resource-exhaustion failures (e.g. ENFILE) before the
     * real fd is returned.  This lets tests verify that the acceptor does NOT
     * close the listen socket on such errors. */
    if (mock_accept_fail_count > 0) {
        mock_accept_fail_count--;
        errno = mock_accept_errno;
        return -1;
    }

    /* Return configured client fd on the first non-failure call, then -1 to
     * stop the acceptor loop (simulates exit-request path via errno != ENFILE). */
    if (mock_accept_fd >= 0) {
        int fd = mock_accept_fd;
        mock_accept_fd = -2;   /* -2 = "already consumed" sentinel */
        return fd;
    }
    return -1;
}

ssize_t mock_recv(int sockfd, void *buf, size_t len, int flags)
{
    (void)sockfd;
    (void)flags;

    /* Let a test observe descriptor state at the exact moment recv() is entered,
     * i.e. before the receiver has processed any data. */
    if (mock_recv_hook) {
        mock_recv_hook(mock_recv_call_count);
    }

    if (mock_recv_call_count < mock_recv_return_count) {
        int ret = mock_recv_return_values[mock_recv_call_count];
        mock_recv_call_count++;

        if ((ret > 0) && buf) {
            int copy_len = ((size_t)ret < len) ? ret : (int)len;
            memcpy(buf, mock_recv_data, (size_t)copy_len);
        }
        return (ssize_t)ret;
    }

    /* No more configured values — return 0 (connection closed) */
    mock_recv_call_count++;
    return 0;
}

ssize_t mock_send(int sockfd, const void *buf, size_t len, int flags)
{
    (void)buf;
    (void)flags;
    mock_send_call_count++;
    mock_send_last_fd = sockfd;
    return (ssize_t)len;
}

int mock_close(int fd)
{
    if (mock_close_hook) {
        mock_close_hook(fd);
    }
    mock_close_call_count++;
    return 0;
}

int mock_shutdown(int sockfd, int how)
{
    (void)sockfd;
    (void)how;
    mock_shutdown_call_count++;
    return 0;
}

int mock_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    mock_setsockopt_call_count++;
    return 0;
}

char *mock_inet_ntoa_r(struct in_addr in, char *buf, socklen_t size)
{
    (void)in;
    if (buf && (size > 0)) {
        buf[0] = '\0';
    }
    return buf;
}

/* ── Reset ──────────────────────────────────────────────────────────────── */

void mock_lwip_sockets_reset(void)
{
    memset(mock_recv_return_values, 0, sizeof(mock_recv_return_values));
    mock_recv_return_count = 0;
    mock_recv_call_count = 0;
    memset(mock_recv_data, 0, sizeof(mock_recv_data));
    mock_recv_data_len = 0;
    mock_recv_hook = 0;

    mock_socket_fd = 5;
    mock_socket_should_fail = false;
    mock_bind_should_fail = false;
    mock_listen_should_fail = false;

    mock_socket_last_domain = -1;
    mock_socket_last_type = -1;
    mock_socket_last_protocol = -1;
    mock_bind_call_count = 0;
    /* 0xEE, not 0: a test asserting that the bound address arrived fully zeroed must fail
     * when bind() was never reached, instead of reading a conveniently zeroed buffer. */
    memset(&mock_bind_last_addr, 0xEE, sizeof(mock_bind_last_addr));
    mock_bind_last_addrlen = 0;
    mock_accept_fd = 10;
    mock_accept_call_count = 0;
    mock_accept_fail_count = 0;
    mock_accept_errno = ENFILE;

    mock_close_call_count = 0;
    mock_shutdown_call_count = 0;
    mock_send_call_count = 0;
    mock_setsockopt_call_count = 0;
    mock_send_last_fd = -1;
    mock_close_hook = 0;
}
