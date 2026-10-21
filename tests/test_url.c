#include "tc_test.h"

#include <stdlib.h>

#include "tc_url.h"
#include "url_cases.h"

void tc_suite_url(void);

/* The URL object's attribute getters from the URL Standard. */
static const char *attr(const tc_url *u, const char *name, char *scratch, size_t size)
{
    if (strcmp(name, "href") == 0)
        return u->href;
    if (strcmp(name, "protocol") == 0) {
        snprintf(scratch, size, "%s:", u->scheme);
        return scratch;
    }
    if (strcmp(name, "username") == 0)
        return u->username;
    if (strcmp(name, "password") == 0)
        return u->password;
    if (strcmp(name, "host") == 0) {
        if (!u->host)
            return "";
        if (u->port < 0)
            return u->host;
        snprintf(scratch, size, "%s:%d", u->host, u->port);
        return scratch;
    }
    if (strcmp(name, "hostname") == 0)
        return u->host ? u->host : "";
    if (strcmp(name, "port") == 0) {
        if (u->port < 0)
            return "";
        snprintf(scratch, size, "%d", u->port);
        return scratch;
    }
    if (strcmp(name, "pathname") == 0)
        return u->path;
    if (strcmp(name, "search") == 0) {
        if (!u->query || !u->query[0])
            return "";
        snprintf(scratch, size, "?%s", u->query);
        return scratch;
    }
    if (u->fragment == NULL || !u->fragment[0])
        return "";
    snprintf(scratch, size, "#%s", u->fragment);
    return scratch;
}

/* Returns NULL if the case passes, else a description of the first mismatch. */
static const char *run_case(const url_case *t, char *why, size_t size)
{
    static const char *names[] = { "href", "protocol", "username", "password", "host",
                                   "hostname", "port", "pathname", "search", "hash" };
    const char *expected[10];
    tc_url base, url;
    int err, i;

    if (t->base && tc_url_parse(t->base, NULL, &base) != TC_OK) {
        snprintf(why, size, "base URL failed to parse");
        return why;
    }
    err = tc_url_parse_n(t->input, t->input_len, t->base ? &base : NULL, &url);
    if (t->base)
        tc_url_free(&base);
    if (t->failure) {
        if (err == TC_OK) {
            snprintf(why, size, "parsed as \"%s\" but should fail", url.href);
            tc_url_free(&url);
            return why;
        }
        return NULL;
    }
    if (err != TC_OK) {
        snprintf(why, size, "failed to parse, expected \"%s\"", t->href);
        return why;
    }
    expected[0] = t->href, expected[1] = t->protocol, expected[2] = t->username;
    expected[3] = t->password, expected[4] = t->host, expected[5] = t->hostname;
    expected[6] = t->port, expected[7] = t->pathname, expected[8] = t->search, expected[9] = t->hash;
    for (i = 0; i < 10; i++) {
        char scratch[2048];
        const char *got = attr(&url, names[i], scratch, sizeof scratch);
        if (expected[i] && strcmp(got, expected[i]) != 0) {
            snprintf(why, size, "%s is \"%.200s\", expected \"%.200s\"", names[i], got, expected[i]);
            tc_url_free(&url);
            return why;
        }
    }
    tc_url_free(&url);
    return NULL;
}

static void test_web_platform_tests(void)
{
    size_t i, passed = 0, known = 0;
    char why[600];

    for (i = 0; i < url_case_count; i++) {
        const url_case *t = &url_cases[i];
        const char *problem = run_case(t, why, sizeof why);
        if (!problem && !t->known_failure) {
            passed++;
        } else if (problem && t->known_failure) {
            known++;
        } else if (problem) {
            char msg[900];
            snprintf(msg, sizeof msg, "\"%.120s\" (base \"%.80s\"): %s", t->input,
                     t->base ? t->base : "none", problem);
            TC_CHECK_STR(msg, "a pass");
        } else {
            char msg[400];
            snprintf(msg, sizeof msg, "\"%.120s\" now passes: remove it from url_known_failures.txt",
                     t->input);
            TC_CHECK_STR(msg, "a known failure");
        }
    }
    printf("     web-platform-tests URL: %u of %u pass, %u known failures\n", (unsigned)passed,
           (unsigned)url_case_count, (unsigned)known);
}

static void test_web_platform_tests_hosts(void)
{
    size_t i, passed = 0, known = 0;

    for (i = 0; i < host_case_count; i++) {
        const host_case *t = &host_cases[i];
        char input[512], why[400];
        const char *problem = why;
        tc_url url;
        int err;

        memcpy(input, "https://", 8);
        memcpy(input + 8, t->input, t->input_len);
        memcpy(input + 8 + t->input_len, "/x", 2);
        err = tc_url_parse_n(input, t->input_len + 10, NULL, &url);
        if (!t->output && err == TC_OK)
            snprintf(why, sizeof why, "host \"%s\" but should fail", url.host);
        else if (t->output && err != TC_OK)
            snprintf(why, sizeof why, "failed, expected \"%s\"", t->output);
        else if (t->output && strcmp(url.host, t->output) != 0)
            snprintf(why, sizeof why, "host \"%s\", expected \"%s\"", url.host, t->output);
        else
            problem = NULL;
        if (err == TC_OK)
            tc_url_free(&url);

        if (!problem && !t->known_failure) {
            passed++;
        } else if (problem && t->known_failure) {
            known++;
        } else {
            char msg[600];
            snprintf(msg, sizeof msg, "host \"%.100s\": %s", t->input,
                     problem ? problem : "now passes: remove it from url_known_failures.txt");
            TC_CHECK_STR(msg, "a pass");
        }
    }
    printf("     web-platform-tests hosts: %u of %u pass, %u known failures\n", (unsigned)passed,
           (unsigned)host_case_count, (unsigned)known);
}

static void test_resolving_links(void)
{
    tc_url base, url;
    TC_CHECK_INT(tc_url_parse("https://en.wikipedia.org/wiki/Windows_95?a=1#top", NULL, &base), TC_OK);
    TC_CHECK_INT(tc_url_parse("../w/index.php?title=Win95", &base, &url), TC_OK);
    TC_CHECK_STR(url.href, "https://en.wikipedia.org/w/index.php?title=Win95");
    tc_url_free(&url);
    TC_CHECK_INT(tc_url_parse("//upload.wikimedia.org/x.png", &base, &url), TC_OK);
    TC_CHECK_STR(url.href, "https://upload.wikimedia.org/x.png");
    tc_url_free(&url);
    TC_CHECK_INT(tc_url_parse("#history", &base, &url), TC_OK);
    TC_CHECK_STR(url.href, "https://en.wikipedia.org/wiki/Windows_95?a=1#history");
    tc_url_free(&url);
    tc_url_free(&base);
}

static void test_request_target_and_ports(void)
{
    tc_url url;
    tc_buf target;
    tc_buf_init(&target);
    TC_CHECK_INT(tc_url_parse("HTTPS://Example.COM:443/a%20b?q=1#frag", NULL, &url), TC_OK);
    TC_CHECK_STR(url.host, "example.com");
    TC_CHECK_INT(url.port, -1);
    TC_CHECK_INT(tc_url_effective_port(&url), 443);
    tc_url_request_target(&url, &target);
    TC_CHECK_STR(tc_buf_cstr(&target), "/a%20b?q=1");
    tc_url_free(&url);
    TC_CHECK_INT(tc_url_parse("http://127.1:8080", NULL, &url), TC_OK);
    TC_CHECK_STR(url.host, "127.0.0.1");
    TC_CHECK_INT(tc_url_effective_port(&url), 8080);
    tc_url_request_target(&url, &target);
    TC_CHECK_STR(tc_buf_cstr(&target), "/");
    tc_url_free(&url);
    TC_CHECK_INT(tc_url_parse("http://exa mple.com/", NULL, &url), TC_ERR_URL);
    tc_buf_free(&target);
}

static void test_internationalised_hosts(void)
{
    tc_url url;
    TC_CHECK_INT(tc_url_parse("https://bücher.example/", NULL, &url), TC_OK);
    TC_CHECK_STR(url.host, "xn--bcher-kva.example");
    tc_url_free(&url);
    TC_CHECK_INT(tc_url_parse("https://ＥＸＡＭＰＬＥ。com/", NULL, &url), TC_OK);
    TC_CHECK_STR(url.host, "example.com");
    tc_url_free(&url);
}

void tc_suite_url(void)
{
    TC_RUN(test_web_platform_tests);
    TC_RUN(test_web_platform_tests_hosts);
    TC_RUN(test_resolving_links);
    TC_RUN(test_request_target_and_ports);
    TC_RUN(test_internationalised_hosts);
}
