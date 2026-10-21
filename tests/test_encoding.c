#include "tc_encoding.h"
#include "tc_html_sniff.h"
#include "tc_mime.h"
#include "tc_test.h"

void tc_suite_encoding(void);

static tc_encoding label(const char *s)
{
    return tc_encoding_from_label(s, strlen(s));
}

/* Decode and compare with the expected UTF-8. */
static int decodes(tc_encoding enc, const char *in, size_t len, const char *expected)
{
    tc_buf out;
    int ok;

    tc_buf_init(&out);
    ok = tc_decode(enc, in, len, &out) == TC_OK && strcmp(tc_buf_cstr(&out), expected) == 0 &&
         out.len == strlen(expected);
    if (!ok)
        printf("     decoded as \"%s\"\n", tc_buf_cstr(&out));
    tc_buf_free(&out);
    return ok;
}

static void test_names_and_labels(void)
{
    TC_CHECK_STR(tc_encoding_name(TC_ENC_UTF_8), "UTF-8");
    TC_CHECK_STR(tc_encoding_name(TC_ENC_WINDOWS_1252), "windows-1252");
    TC_CHECK_STR(tc_encoding_name(TC_ENC_X_USER_DEFINED), "x-user-defined");
    TC_CHECK_STR(tc_encoding_name(TC_ENC_NONE), "");
    TC_CHECK_INT(label("utf-8"), TC_ENC_UTF_8);
    TC_CHECK_INT(label(" UTF8\t"), TC_ENC_UTF_8);
    TC_CHECK_INT(label("latin1"), TC_ENC_WINDOWS_1252);
    TC_CHECK_INT(label("ISO-8859-1"), TC_ENC_WINDOWS_1252);
    TC_CHECK_INT(label("us-ascii"), TC_ENC_WINDOWS_1252);
    TC_CHECK_INT(label("iso-8859-9"), TC_ENC_WINDOWS_1254);
    TC_CHECK_INT(label("tis-620"), TC_ENC_WINDOWS_874);
    TC_CHECK_INT(label("utf-16"), TC_ENC_UTF_16LE);
    TC_CHECK_INT(label("iso-2022-kr"), TC_ENC_REPLACEMENT);
    TC_CHECK_INT(label("Shift_JIS"), TC_ENC_SHIFT_JIS);
    TC_CHECK_INT(label("utf-7"), TC_ENC_NONE);
    TC_CHECK_INT(label(""), TC_ENC_NONE);
    TC_CHECK_INT(tc_encoding_from_label("utf-8\0x", 7), TC_ENC_NONE);
    TC_CHECK(tc_encoding_supported(TC_ENC_KOI8_U));
    TC_CHECK(!tc_encoding_supported(TC_ENC_SHIFT_JIS));
    TC_CHECK(!tc_encoding_supported(TC_ENC_NONE));
}

static void test_decoders(void)
{
    /* UTF-8: each maximal invalid subpart becomes one U+FFFD. */
    TC_CHECK(decodes(TC_ENC_UTF_8, "caf\xC3\xA9", 5, "caf\xC3\xA9"));
    TC_CHECK(decodes(TC_ENC_UTF_8, "a\xF0\x9F\x98", 4, "a\xEF\xBF\xBD"));
    TC_CHECK(decodes(TC_ENC_UTF_8, "\xC0\xAF", 2, "\xEF\xBF\xBD\xEF\xBF\xBD"));
    /* A BOM wins over the given encoding and is dropped. */
    TC_CHECK(decodes(TC_ENC_WINDOWS_1252, "\xEF\xBB\xBFok", 5, "ok"));
    TC_CHECK(decodes(TC_ENC_UTF_8, "\xFF\xFE" "h\0i\0", 6, "hi"));
    /* windows-1252: 0x80 is the euro sign; 0x81 maps to U+0081. */
    TC_CHECK(decodes(TC_ENC_WINDOWS_1252, "\x80\x81\xE9", 3, "\xE2\x82\xAC\xC2\x81\xC3\xA9"));
    /* KOI8-R and ISO-8859-8-I (which shares ISO-8859-8's table). */
    TC_CHECK(decodes(TC_ENC_KOI8_R, "\xC1", 1, "\xD0\xB0"));
    TC_CHECK(decodes(TC_ENC_ISO_8859_8_I, "\xE0", 1, "\xD7\x90"));
    /* Bytes a single-byte encoding leaves undefined are errors. */
    TC_CHECK(decodes(TC_ENC_WINDOWS_874, "\xDB", 1, "\xEF\xBF\xBD"));
    /* UTF-16: pairs, lone surrogates and an odd byte at the end. */
    TC_CHECK(decodes(TC_ENC_UTF_16BE, "\xD8\x3D\xDE\x00", 4, "\xF0\x9F\x98\x80"));
    TC_CHECK(decodes(TC_ENC_UTF_16LE, "\x3D\xD8" "a\0", 4, "\xEF\xBF\xBD" "a"));
    TC_CHECK(decodes(TC_ENC_UTF_16LE, "\x00\xDC" "a\0", 4, "\xEF\xBF\xBD" "a"));
    TC_CHECK(decodes(TC_ENC_UTF_16LE, "a\0b", 3, "a\xEF\xBF\xBD"));
    TC_CHECK(decodes(TC_ENC_UTF_16LE, "\x3D\xD8", 2, "\xEF\xBF\xBD"));
    /* x-user-defined maps the high half to the Private Use Area. */
    TC_CHECK(decodes(TC_ENC_X_USER_DEFINED, "a\x80\xFF", 3, "a\xEF\x9E\x80\xEF\x9F\xBF"));
    /* replacement: any input at all is one error. */
    TC_CHECK(decodes(TC_ENC_REPLACEMENT, "anything", 8, "\xEF\xBF\xBD"));
    TC_CHECK(decodes(TC_ENC_REPLACEMENT, "", 0, ""));
    /* Not decodable yet: windows-1252 instead. */
    TC_CHECK(decodes(TC_ENC_SHIFT_JIS, "a\x80", 2, "a\xE2\x82\xAC"));
}

static void test_mime(void)
{
    tc_mime m;

    TC_CHECK_INT(tc_mime_parse("text/html; charset=UTF-8", &m), TC_OK);
    TC_CHECK_STR(m.essence, "text/html");
    TC_CHECK_STR(m.charset, "UTF-8");
    TC_CHECK(tc_mime_is_html(&m));
    TC_CHECK_INT(tc_mime_parse(" Text/HTML ;Charset=\"iso-8859-1\" ; x=y", &m), TC_OK);
    TC_CHECK_STR(m.essence, "text/html");
    TC_CHECK_STR(m.charset, "iso-8859-1");
    TC_CHECK_INT(tc_mime_parse("text/plain;charset=\"a\\\"b\"junk;charset=second", &m), TC_OK);
    TC_CHECK_STR(m.charset, "a\"b");
    TC_CHECK_INT(tc_mime_parse("text/html;charset=;charset=utf-8", &m), TC_OK);
    TC_CHECK_STR(m.charset, "utf-8");
    TC_CHECK_INT(tc_mime_parse("text/html;charset", &m), TC_OK);
    TC_CHECK_STR(m.charset, "");
    TC_CHECK_INT(tc_mime_parse("application/xhtml+xml", &m), TC_OK);
    TC_CHECK(!tc_mime_is_html(&m));
    TC_CHECK_INT(tc_mime_parse("text", &m), TC_ERR_INVALID);
    TC_CHECK_INT(tc_mime_parse("/html", &m), TC_ERR_INVALID);
    TC_CHECK_INT(tc_mime_parse("te xt/html", &m), TC_ERR_INVALID);
    TC_CHECK_INT(tc_mime_parse("", &m), TC_ERR_INVALID);

    TC_CHECK(tc_mime_sniff_html("  <!DOCTYPE html>", 17));
    TC_CHECK(tc_mime_sniff_html("<P>hello", 8));
    TC_CHECK(tc_mime_sniff_html("<!-- x", 6));
    TC_CHECK(!tc_mime_sniff_html("<pre>", 5));
    TC_CHECK(!tc_mime_sniff_html("<html", 5)); /* no terminator yet */
    TC_CHECK(!tc_mime_sniff_html("{\"json\": 1}", 11));
    TC_CHECK(tc_mime_wants_html("text/html", "", 0, &m));
    TC_CHECK(tc_mime_wants_html("application/xhtml+xml; charset=utf-8", "", 0, &m));
    TC_CHECK_STR(m.charset, "utf-8");
    TC_CHECK(!tc_mime_wants_html("text/plain", "<html>", 6, &m));
    TC_CHECK(tc_mime_wants_html(NULL, "<html>", 6, &m));
    TC_CHECK(tc_mime_wants_html("bogus", "<html>", 6, &m));
}

static tc_encoding meta(const char *content)
{
    return tc_html_meta_charset(content, strlen(content));
}

static tc_encoding prescan(const char *html)
{
    return tc_html_prescan(html, strlen(html));
}

static void test_meta_and_prescan(void)
{
    tc_confidence confidence;
    tc_encoding_source source;
    const char *page = "<html><head><title>x</title></head>";

    TC_CHECK_INT(meta("text/html; charset=utf-8"), TC_ENC_UTF_8);
    TC_CHECK_INT(meta("text/html; CHARSET = 'koi8-r'"), TC_ENC_KOI8_R);
    TC_CHECK_INT(meta("charset;charset=latin2"), TC_ENC_ISO_8859_2);
    TC_CHECK_INT(meta("charset=\"utf-8"), TC_ENC_NONE);
    TC_CHECK_INT(meta("charset="), TC_ENC_NONE);
    TC_CHECK_INT(meta("text/html"), TC_ENC_NONE);

    TC_CHECK_INT(prescan("<meta charset=\"utf-8\">"), TC_ENC_UTF_8);
    TC_CHECK_INT(prescan("<!-- <meta charset=koi8-r> --><meta charset=latin2>"), TC_ENC_ISO_8859_2);
    TC_CHECK_INT(prescan("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">"), TC_ENC_UTF_8);
    /* A content charset counts only with the http-equiv pragma. */
    TC_CHECK_INT(prescan("<meta content=\"text/html; charset=utf-8\">"), TC_ENC_NONE);
    /* UTF-16 declarations mean UTF-8: the bytes are evidently ASCII-compatible. */
    TC_CHECK_INT(prescan("<meta charset=utf-16>"), TC_ENC_UTF_8);
    TC_CHECK_INT(prescan("<meta charset=x-user-defined>"), TC_ENC_WINDOWS_1252);
    TC_CHECK_INT(prescan("<?xml version=\"1.0\" encoding=\"windows-1251\"?><p>"), TC_ENC_WINDOWS_1251);
    TC_CHECK_INT(prescan("<meta charset=utf-8"), TC_ENC_NONE); /* ran out of bytes */

    TC_CHECK_INT(tc_html_sniff(page, strlen(page), "utf-8", &confidence, &source), TC_ENC_UTF_8);
    TC_CHECK_INT(confidence, TC_CONFIDENCE_CERTAIN);
    TC_CHECK_INT(source, TC_ENCSRC_HTTP);
    TC_CHECK_INT(tc_html_sniff(page, strlen(page), "bogus", &confidence, &source), TC_ENC_WINDOWS_1252);
    TC_CHECK_INT(confidence, TC_CONFIDENCE_TENTATIVE);
    TC_CHECK_INT(source, TC_ENCSRC_DEFAULT);
    TC_CHECK_INT(tc_html_sniff("\xEF\xBB\xBF<p>", 6, "latin1", &confidence, &source), TC_ENC_UTF_8);
    TC_CHECK_INT(source, TC_ENCSRC_BOM);
    TC_CHECK_INT(tc_html_sniff("<meta charset=latin2>", 21, NULL, &confidence, &source), TC_ENC_ISO_8859_2);
    TC_CHECK_INT(source, TC_ENCSRC_PRESCAN);
    TC_CHECK_INT(confidence, TC_CONFIDENCE_TENTATIVE);
}

void tc_suite_encoding(void)
{
    TC_RUN(test_names_and_labels);
    TC_RUN(test_decoders);
    TC_RUN(test_mime);
    TC_RUN(test_meta_and_prescan);
}
