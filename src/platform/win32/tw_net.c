/* tc_platform.h networking for Windows 95: Winsock 1.1 (WSOCK32.DLL), which
 * every copy of Windows 95 has. Sockets are non-blocking and waited on with
 * select() in short slices, so a cancelled load stops promptly. */
#include <windows.h>
#include <winsock.h>
#include <stdlib.h>
#include <string.h>

#include "tc_platform.h"

/* How often a blocking call looks at its cancel flag. */
#define SLICE_MS 200

struct tc_socket {
    SOCKET s;
};

int tc_net_init(void)
{
    WSADATA wsa;
    return WSAStartup(MAKEWORD(1, 1), &wsa) == 0 ? TC_OK : TC_ERR_NET;
}

int tc_net_resolve(const char *host, uint8_t addr[4])
{
    unsigned long ip = inet_addr(host);
    struct hostent *he;

    if (ip != INADDR_NONE || strcmp(host, "255.255.255.255") == 0) {
        memcpy(addr, &ip, 4);
        return TC_OK;
    }
    /* Winsock keeps the result per thread, so this is safe off the UI thread. */
    he = gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET || he->h_length != 4 || !he->h_addr_list[0])
        return TC_ERR_DNS;
    memcpy(addr, he->h_addr_list[0], 4);
    return TC_OK;
}

enum wait_for { WAIT_READ, WAIT_WRITE, WAIT_CONNECT };

/* Wait until the socket can be read or written, or a non-blocking connect
   has finished: success shows as writable, failure in the exception set
   (which, once connected, would only mean out-of-band data). */
static int wait_ready(SOCKET s, enum wait_for what, uint32_t timeout_ms, const volatile int *cancel)
{
    uint32_t start = GetTickCount();

    for (;;) {
        fd_set ready, failed;
        struct timeval tv;
        uint32_t elapsed = GetTickCount() - start, slice;
        int n;

        if (cancel && *cancel)
            return TC_ERR_CANCELLED;
        if (elapsed >= timeout_ms)
            return TC_ERR_TIMEOUT;
        slice = TC_MIN(timeout_ms - elapsed, SLICE_MS);
        FD_ZERO(&ready);
        FD_ZERO(&failed);
        FD_SET(s, &ready);
        FD_SET(s, &failed);
        tv.tv_sec = 0;
        tv.tv_usec = (long)slice * 1000;
        n = select(0, what == WAIT_READ ? &ready : NULL, what == WAIT_READ ? NULL : &ready,
                   what == WAIT_CONNECT ? &failed : NULL, &tv);
        if (n == SOCKET_ERROR)
            return TC_ERR_NET;
        if (n > 0)
            return FD_ISSET(s, &failed) ? TC_ERR_CONNECT : TC_OK;
    }
}

int tc_net_connect(const uint8_t addr[4], uint16_t port, uint32_t timeout_ms,
                   const volatile int *cancel, tc_socket **out)
{
    struct sockaddr_in sa;
    u_long nonblocking = 1;
    tc_socket *sock;
    int err;

    *out = NULL;
    sock = malloc(sizeof *sock);
    if (!sock)
        return TC_ERR_NOMEM;
    sock->s = socket(AF_INET, SOCK_STREAM, 0);
    if (sock->s == INVALID_SOCKET) {
        free(sock);
        return TC_ERR_NET;
    }
    ioctlsocket(sock->s, FIONBIO, &nonblocking);

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, addr, 4);
    if (connect(sock->s, (struct sockaddr *)&sa, sizeof sa) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK) {
        tc_net_close(sock);
        return TC_ERR_CONNECT;
    }
    err = wait_ready(sock->s, WAIT_CONNECT, timeout_ms, cancel);
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
        int n = send(s->s, data, (int)TC_MIN(len, 0x7FFFFFFF), 0);
        int err;
        if (n > 0)
            return n;
        if (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            return TC_ERR_NET;
        err = wait_ready(s->s, WAIT_WRITE, timeout_ms, cancel);
        if (err)
            return err;
    }
}

int tc_net_recv(tc_socket *s, void *buf, size_t len, uint32_t timeout_ms,
                const volatile int *cancel)
{
    for (;;) {
        int n = recv(s->s, buf, (int)TC_MIN(len, 0x7FFFFFFF), 0);
        int err;
        if (n >= 0)
            return n;
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            return WSAGetLastError() == WSAECONNRESET ? TC_ERR_CLOSED : TC_ERR_NET;
        err = wait_ready(s->s, WAIT_READ, timeout_ms, cancel);
        if (err)
            return err;
    }
}

void tc_net_close(tc_socket *s)
{
    if (s) {
        closesocket(s->s);
        free(s);
    }
}
