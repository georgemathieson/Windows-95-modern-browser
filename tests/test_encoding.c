#include "tc_encoding.h"
#include "tc_html_sniff.h"
#include "tc_mime.h"
#include "tc_test.h"
#include "tc_utf8.h"

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
    TC_CHECK(tc_encoding_supported(TC_ENC_SHIFT_JIS));
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
}

#define FFFD "\xEF\xBF\xBD"

/* Chinese, Japanese and Korean: a word each, then the corners of each
   decoder in the Encoding Standard. */
static void test_cjk_decoders(void)
{
    /* Shift_JIS: 日本語, half-width katakana, 0x80 as itself, Windows'
       user-defined characters, and a bad trail byte (ASCII, so it's kept). */
    TC_CHECK(decodes(TC_ENC_SHIFT_JIS, "\x93\xFA\x96\x7B\x8C\xEA", 6, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"));
    TC_CHECK(decodes(TC_ENC_SHIFT_JIS, "\xB1\x80", 2, "\xEF\xBD\xB1\xC2\x80"));
    TC_CHECK(decodes(TC_ENC_SHIFT_JIS, "\xF0\x40", 2, "\xEE\x80\x80"));
    TC_CHECK(decodes(TC_ENC_SHIFT_JIS, "\x81 a\x81", 4, FFFD " a" FFFD));
    TC_CHECK(decodes(TC_ENC_SHIFT_JIS, "\xA0\xFD", 2, FFFD FFFD));
    /* EUC-JP: 日本語, half-width katakana, JIS X 0212, errors. */
    TC_CHECK(decodes(TC_ENC_EUC_JP, "\xC6\xFC\xCB\xDC\xB8\xEC", 6, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"));
    TC_CHECK(decodes(TC_ENC_EUC_JP, "\x8E\xB1\x8F\xB0\xA1", 5, "\xEF\xBD\xB1\xE4\xB8\x82"));
    TC_CHECK(decodes(TC_ENC_EUC_JP, "\xC6" "a\x8F\xB0", 4, FFFD "a" FFFD));
    /* ISO-2022-JP: escapes to JIS X 0208, Roman and katakana and back. */
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "\x1B$BF|K\\8l\x1B(B!", 13, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E!"));
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "\x1B(J\\~\x1B(I1", 9, "\xC2\xA5\xE2\x80\xBE\xEF\xBD\xB1"));
    /* Two escapes with nothing between, an unknown escape, one cut short,
       and a lead byte with no trail. */
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "\x1B(B\x1B(Ba", 7, FFFD "a"));
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "\x1B(A", 3, FFFD "(A"));
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "a\x1B", 2, "a" FFFD));
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "a\x1B$", 3, "a" FFFD "$"));
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "\x1B$BF", 4, FFFD));
    TC_CHECK(decodes(TC_ENC_ISO_2022_JP, "\x0E", 1, FFFD));
    /* GBK and gb18030 decode alike: 中文, the euro sign, four-byte
       sequences across Unicode, and their errors. */
    TC_CHECK(decodes(TC_ENC_GBK, "\xD6\xD0\xCE\xC4\x80", 5, "\xE4\xB8\xAD\xE6\x96\x87\xE2\x82\xAC"));
    TC_CHECK(decodes(TC_ENC_GB18030, "\x81\x30\x81\x30\x84\x31\xA4\x39", 8, "\xC2\x80\xEF\xBF\xBF"));
    TC_CHECK(decodes(TC_ENC_GB18030, "\x90\x30\x81\x30\xE3\x32\x9A\x35", 8, "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF"));
    TC_CHECK(decodes(TC_ENC_GB18030, "\x81\x35\xF4\x37", 4, "\xEE\x9F\x87")); /* U+E7C7 */
    TC_CHECK(decodes(TC_ENC_GB18030, "\x84\x31\xA5\x30", 4, FFFD));        /* past U+FFFF, before plane 1 */
    TC_CHECK(decodes(TC_ENC_GB18030, "\x81\x30\x41", 3, FFFD "0A"));
    TC_CHECK(decodes(TC_ENC_GB18030, "\x81\x30\x81\x20", 4, FFFD "0" FFFD " "));
    TC_CHECK(decodes(TC_ENC_GB18030, "\x81\x30\x81", 3, FFFD));
    TC_CHECK(decodes(TC_ENC_GB18030, "\xFF", 1, FFFD));
    /* Big5: 中文, a character in plane 2, and the four pointers that are
       two code points each. */
    TC_CHECK(decodes(TC_ENC_BIG5, "\xA4\xA4\xA4\xE5", 4, "\xE4\xB8\xAD\xE6\x96\x87"));
    TC_CHECK(decodes(TC_ENC_BIG5, "\x87\x45", 2, "\xF0\xA7\x89\xA7"));
    TC_CHECK(decodes(TC_ENC_BIG5, "\x88\x62\x88\xA5", 4, "\xC3\x8A\xCC\x84\xC3\xAA\xCC\x8C"));
    TC_CHECK(decodes(TC_ENC_BIG5, "\xA4 \xA4", 3, FFFD " " FFFD)); /* letters are trail bytes in Big5 */
    /* EUC-KR: 한국어. */
    TC_CHECK(decodes(TC_ENC_EUC_KR, "\xC7\xD1\xB1\xB9\xBE\xEE", 6, "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"));
    TC_CHECK(decodes(TC_ENC_EUC_KR, "\xC7\x20\xFF", 3, FFFD " " FFFD));
}

/* Samples of each index (tools/gen_encoding_tests.py), turned into the bytes
   that encode them in each encoding that uses the index. */
extern const uint32_t tc_test_index_jis0208[], tc_test_index_jis0212[], tc_test_index_gb18030[],
    tc_test_index_big5[], tc_test_index_euc_kr[];
extern const size_t tc_test_index_jis0208_len, tc_test_index_jis0212_len, tc_test_index_gb18030_len,
    tc_test_index_big5_len, tc_test_index_euc_kr_len;

static int decodes_to(tc_encoding enc, const uint8_t *in, size_t len, uint32_t cp)
{
    char want[8];
    size_t n = tc_utf8_encode(cp, want);
    tc_buf out;
    int ok;
    tc_buf_init(&out);
    ok = tc_decode(enc, in, len, &out) == TC_OK && out.len == n && memcmp(out.data, want, n) == 0;
    tc_buf_free(&out);
    return ok;
}

static void check_index(const char *name, const uint32_t *pairs, size_t count)
{
    size_t i;
    unsigned bad = 0, checked = 0;
    for (i = 0; i < count; i++) {
        uint32_t p = pairs[2 * i], cp = pairs[2 * i + 1], t;
        uint8_t b[8];
        int ok = 1;
        if (strcmp(name, "jis0208") == 0) {
            t = p % 188;
            b[0] = (uint8_t)(p / 188 + (p / 188 < 0x1F ? 0x81 : 0xC1));
            b[1] = (uint8_t)(t + (t < 0x3F ? 0x40 : 0x41));
            if (p < 8836 || p > 10715)
                ok &= decodes_to(TC_ENC_SHIFT_JIS, b, 2, cp);
            if (p < 94 * 94) {
                b[0] = (uint8_t)(p / 94 + 0xA1), b[1] = (uint8_t)(p % 94 + 0xA1);
                ok &= decodes_to(TC_ENC_EUC_JP, b, 2, cp);
                memcpy(b, "\x1B$B", 3);
                b[3] = (uint8_t)(p / 94 + 0x21), b[4] = (uint8_t)(p % 94 + 0x21);
                ok &= decodes_to(TC_ENC_ISO_2022_JP, b, 5, cp);
            }
        } else if (strcmp(name, "jis0212") == 0) {
            b[0] = 0x8F, b[1] = (uint8_t)(p / 94 + 0xA1), b[2] = (uint8_t)(p % 94 + 0xA1);
            ok &= decodes_to(TC_ENC_EUC_JP, b, 3, cp);
        } else if (strcmp(name, "gb18030") == 0) {
            t = p % 190;
            b[0] = (uint8_t)(p / 190 + 0x81), b[1] = (uint8_t)(t + (t < 0x3F ? 0x40 : 0x41));
            ok &= decodes_to(TC_ENC_GBK, b, 2, cp) && decodes_to(TC_ENC_GB18030, b, 2, cp);
        } else if (strcmp(name, "big5") == 0) {
            t = p % 157;
            b[0] = (uint8_t)(p / 157 + 0x81), b[1] = (uint8_t)(t + (t < 0x3F ? 0x40 : 0x62));
            ok &= decodes_to(TC_ENC_BIG5, b, 2, cp);
        } else {
            b[0] = (uint8_t)(p / 190 + 0x81), b[1] = (uint8_t)(p % 190 + 0x41);
            ok &= decodes_to(TC_ENC_EUC_KR, b, 2, cp);
        }
        checked++;
        if (!ok && bad++ < 5) {
            char msg[80];
            snprintf(msg, sizeof msg, "index %s pointer %lu: not U+%04lX", name, (unsigned long)p, (unsigned long)cp);
            tc_test_fail(__FILE__, __LINE__, msg);
        }
    }
    tc_test_checks++;
    printf("     index %s: %u sampled pointers, %u wrong\n", name, checked, bad);
}

static void test_cjk_indexes(void)
{
    check_index("jis0208", tc_test_index_jis0208, tc_test_index_jis0208_len);
    check_index("jis0212", tc_test_index_jis0212, tc_test_index_jis0212_len);
    check_index("gb18030", tc_test_index_gb18030, tc_test_index_gb18030_len);
    check_index("big5", tc_test_index_big5, tc_test_index_big5_len);
    check_index("euc-kr", tc_test_index_euc_kr, tc_test_index_euc_kr_len);
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
    TC_RUN(test_cjk_decoders);
    TC_RUN(test_cjk_indexes);
    TC_RUN(test_mime);
    TC_RUN(test_meta_and_prescan);
}
