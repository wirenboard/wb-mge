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
    (void)domain;
    (void)type;
    (void)protocol;
    if (mock_socket_should_fail) {
        return -1;
    }
    return mock_socket_fd;
}

int mock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd;
    (void)addr;
    (void)addrlen;
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

    if (addr) {
        memset(addr, 0, sizeof(struct sockaddr_in));
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
