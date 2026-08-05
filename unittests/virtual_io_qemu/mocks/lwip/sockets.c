/* Undefine the redirect macros so this file can define the mock implementations
 * without infinite recursion. */
#undef socket
#undef bind
#undef setsockopt
#undef close
#undef sendto
#undef recvfrom

#include "lwip/sockets.h"
#include <string.h>

/* ── Captured-datagram store ─────────────────────────────────────────────── */

int      mock_sendto_count = 0;
uint8_t  mock_sendto_data[MOCK_SENDTO_MAX][MOCK_REC_LEN] = {{0}};
int      mock_sendto_len[MOCK_SENDTO_MAX] = {0};

/* ── Mock implementations ────────────────────────────────────────────────── */

int mock_socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    return 3;
}

int mock_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    (void)fd;
    (void)addr;
    (void)len;
    return 0;
}

int mock_setsockopt(int fd, int level, int optname, const void *val, socklen_t len)
{
    (void)fd;
    (void)level;
    (void)optname;
    (void)val;
    (void)len;
    return 0;
}

int mock_close(int fd)
{
    (void)fd;
    return 0;
}

ssize_t mock_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t addrlen)
{
    (void)fd;
    (void)flags;
    (void)dest;
    (void)addrlen;

    /* Record the datagram (capped) so tests can inspect emitted records. */
    if (mock_sendto_count < MOCK_SENDTO_MAX) {
        size_t copy_len = (len < MOCK_REC_LEN) ? len : MOCK_REC_LEN;
        if (buf != NULL) {
            memcpy(mock_sendto_data[mock_sendto_count], buf, copy_len);
        }
        mock_sendto_len[mock_sendto_count] = (int)len;
        mock_sendto_count++;
    }
    return (ssize_t)len;
}

ssize_t mock_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *addrlen)
{
    (void)fd;
    (void)buf;
    (void)len;
    (void)flags;
    (void)src;
    (void)addrlen;
    /* Unused by the tests (the RX task is never run). */
    return -1;
}

/* ── Reset ────────────────────────────────────────────────────────────────── */

void mock_lwip_reset(void)
{
    memset(mock_sendto_data, 0, sizeof(mock_sendto_data));
    memset(mock_sendto_len, 0, sizeof(mock_sendto_len));
    mock_sendto_count = 0;
}
