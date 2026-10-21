# web-platform-tests: URL

Test data for the WHATWG URL Standard, from
https://github.com/web-platform-tests/wpt (`url/resources/`), commit
`46175634c0210e97365ef864020ff3be6358ea41`. BSD 3-Clause license (see
`LICENSE.md`).

- `urltestdata.json`: parsing tests, run by `tests/test_url.c` through the
  table that `tools/gen_url_tests.py` generates at build time.
- `toascii.json`: host-to-ASCII (IDNA) tests, kept for when Tachyon gets full
  IDNA support.

Unmodified. To update, copy the files from a newer commit and update the
commit above.
