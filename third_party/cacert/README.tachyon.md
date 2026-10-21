# Root certificates

`cacert.pem` holds the certificate authorities Tachyon trusts: Mozilla's root
store (the one Firefox uses, from NSS's `certdata.txt`), as packaged by
certifi.

| | |
|---|---|
| Upstream | https://github.com/certifi/python-certifi (`certifi/cacert.pem`) |
| Version | **2026.10.19** (120 roots) |
| Commit | `662df63d7906df66f1f07c0751a3dfe9da14d914` |
| License | Mozilla Public License 2.0 (the certificate data comes from Mozilla) |

Unmodified. At build time `tools/gen_cacerts.py` converts it to DER and
compiles it into Tachyon (`tc_cacerts.c`), so there is no separate file to
lose.

To update, copy `cacert.pem` from a newer certifi release and update the
table above. Tachyon releases should always carry a recent bundle: roots
expire and get distrusted.
