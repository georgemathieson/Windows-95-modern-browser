#include "tc_base.h"

const char *tc_err_text(int err)
{
    switch (err) {
    case TC_OK: return "no error";
    case TC_ERR_NOMEM: return "out of memory";
    case TC_ERR_INVALID: return "invalid argument";
    case TC_ERR_TIMEOUT: return "timed out";
    case TC_ERR_CANCELLED: return "cancelled";
    case TC_ERR_URL: return "not a valid web address";
    case TC_ERR_UNSUPPORTED: return "not supported";
    case TC_ERR_DNS: return "server not found";
    case TC_ERR_CONNECT: return "connection refused";
    case TC_ERR_NET: return "network error";
    case TC_ERR_CLOSED: return "connection closed by the server";
    case TC_ERR_TLS: return "secure connection failed";
    case TC_ERR_CERT: return "certificate not trusted";
    case TC_ERR_PROTOCOL: return "invalid response from the server";
    case TC_ERR_REDIRECTS: return "too many redirects";
    case TC_ERR_TOO_LARGE: return "response too large";
    case TC_ERR_DECODE: return "could not decompress the response";
    case TC_ERR_PROXY: return "proxy error";
    }
    return "unknown error";
}
