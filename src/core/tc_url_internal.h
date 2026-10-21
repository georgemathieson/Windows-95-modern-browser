/* Helpers shared by tc_url.c and tc_url_host.c. Not part of the API. */
#ifndef TC_URL_INTERNAL_H
#define TC_URL_INTERNAL_H

#include "tc_url.h"

/* The URL Standard's percent-encode sets. */
enum url_encode_set {
    SET_C0,            /* C0 controls and everything above '~' */
    SET_FRAGMENT,      /* C0 + space " < > ` */
    SET_QUERY,         /* C0 + space " # < > */
    SET_SPECIAL_QUERY, /* query + ' */
    SET_PATH,          /* query + ? ^ ` { } */
    SET_USERINFO       /* path + / : ; = @ [ \ ] | */
};

int url_in_encode_set(unsigned char c, enum url_encode_set set);
/* Append byte c, percent-encoded if it's in the set. */
int url_append_encoded(tc_buf *out, unsigned char c, enum url_encode_set set);
/* Percent-decode bytes ("%41" becomes "A"; a stray '%' stays). */
int url_percent_decode(const char *s, size_t len, tc_buf *out);

int url_is_ascii_alpha(int c);
int url_is_ascii_digit(int c);
int url_is_ascii_hex(int c);
int url_hex_value(int c);

#endif
