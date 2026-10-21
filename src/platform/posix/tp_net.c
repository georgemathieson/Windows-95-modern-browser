/* tc_platform.h networking for the Linux host: BSD sockets, non-blocking,
 * waited on with poll() in short slices like the Windows 95 version. */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tc_platform.h"

#define SLICE_MS 200

struct tc_socket {
    int fd;
};

int tc_net_init(void)
{
    return TC_OK;
}

int tc_net_resolve(const char *host, uint8_t addr[4])
{
    struct addrinfo hints, *res = NULL;
    struct in_addr in;

    if (inet_pton(AF_INET, host, &in) == 1) {
        memcpy(addr, &in, 4);
        return TC_OK;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res)
        return TC_ERR_DNS;
    memcpy(addr, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4);
    freeaddrinfo(res);
    return TC_OK;
}

static int wait_ready(int fd, short events, uint32_t timeout_ms, const volatile int *cancel)
{
    uint32_t start = tc_time_ms();

    for (;;) {
        struct pollfd p;
        uint32_t elapsed = tc_time_ms() - start;
        int n;

        if (cancel && *cancel)
            return TC_ERR_CANCELLED;
        if (elapsed >= timeout_ms)
            return TC_ERR_TIMEOUT;
        p.fd = fd;
        p.events = events;
        p.revents = 0;
        n = poll(&p, 1, (int)TC_MIN(timeout_ms - elapsed, SLICE_MS));
        if (n < 0 && errno != EINTR)
            return TC_ERR_NET;
        if (n > 0)
            return TC_OK; /* errors surface from the following call */
    }
}

int tc_net_connect(const uint8_t addr[4], uint16_t port, uint32_t timeout_ms,
                   const volatile int *cancel, tc_socket **out)
{
    struct sockaddr_in sa;
    tc_socket *sock;
    int err, soerr = 0;
    socklen_t len = sizeof soerr;

    *out = NULL;
    sock = malloc(sizeof *sock);
    if (!sock)
        return TC_ERR_NOMEM;
    sock->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock->fd < 0) {
        free(sock);
        return TC_ERR_NET;
    }
    fcntl(sock->fd, F_SETFL, fcntl(sock->fd, F_GETFL) | O_NONBLOCK);

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, addr, 4);
    if (connect(sock->fd, (struct sockaddr *)&sa, sizeof sa) != 0 && errno != EINPROGRESS) {
        tc_net_close(sock);
        return TC_ERR_CONNECT;
    }
    err = wait_ready(sock->fd, POLLOUT, timeout_ms, cancel);
    if (!err && (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0 || soerr != 0))
        err = TC_ERR_CONNECT;
    if (err) {
        tc_net_close(sock);
        return err;
    }
    *out = sock;
    return TC_OK;
}

int tc_net_send(tc_socket *s, const void *data, size_t len, uint32_t timeout_ms,
                const volatile int *cancel)
{
    for (;;) {
        ssize_t n = send(s->fd, data, len, MSG_NOSIGNAL);
        int err;
        if (n > 0)
            return (int)TC_MIN((size_t)n, 0x7FFFFFFF);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return TC_ERR_NET;
        err = wait_ready(s->fd, POLLOUT, timeout_ms, cancel);
        if (err)
            return err;
    }
}

int tc_net_recv(tc_socket *s, void *buf, size_t len, uint32_t timeout_ms,
                const volatile int *cancel)
{
    for (;;) {
        ssize_t n = recv(s->fd, buf, TC_MIN(len, 0x7FFFFFFF), 0);
        int err;
        if (n >= 0)
            return (int)n;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return errno == ECONNRESET ? TC_ERR_CLOSED : TC_ERR_NET;
        err = wait_ready(s->fd, POLLIN, timeout_ms, cancel);
        if (err)
            return err;
    }
}

void tc_net_close(tc_socket *s)
{
    if (s) {
        close(s->fd);
        free(s);
    }
}
