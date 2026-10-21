# Mbed TLS in Tachyon

| | |
|---|---|
| Upstream | https://github.com/Mbed-TLS/mbedtls |
| Version | **4.1.1** (4.1 LTS, supported until March 2029) |
| Commit | `0a8fda272a5a0abef3b47c91bed37185d5a726b1` (tag `v4.1.1`) |
| TF-PSA-Crypto | `a0632be94d883daa0295ac1eabf70359ad94f91b` (the submodule commit at that tag) |
| License | Apache-2.0 OR GPL-2.0-or-later; Tachyon uses it under Apache-2.0 (see `LICENSE`) |

## What's here

Only what the library needs to build: `include/`, `library/`, and from
TF-PSA-Crypto: `include/`, `core/`, `dispatch/`, `drivers/builtin/`,
`platform/`, `utilities/` and `extras/`. Tests, programs, documentation,
scripts, the build system and the optional p256-m, Everest and PQCP drivers
are left out.

These files don't exist in the git tag; they were generated with upstream's
own `make_generated_files.py` from the same commits:

- `library/error.c`, `library/version_features.c`,
  `library/ssl_debug_helpers_generated.c`,
  `library/mbedtls_config_check_{before,user,final}.h`
- `tf-psa-crypto/core/psa_crypto_driver_wrappers.h`,
  `tf-psa-crypto/core/psa_crypto_driver_wrappers_no_static.c`,
  `tf-psa-crypto/core/tf_psa_crypto_config_check_{before,user,final}.h`

## Local changes

**No upstream file is modified.** Tachyon's settings live in `tachyon/`:

- `tachyon_mbedtls_config.h` (TLS and X.509): client only, no Winsock 2
  socket layer.
- `tachyon_crypto_config.h` (TF-PSA-Crypto): Tachyon's entropy pool and
  clock, no file access, no AES-NI (Windows 95 can't run SSE), threading via
  Tachyon's primitives, MinGW's C99 `snprintf`.
- `threading_alt.h`: mutex types for `MBEDTLS_THREADING_ALT`.

`CMakeLists.txt` is Tachyon's own.

## Updating

1. Clone the new tag with its `tf-psa-crypto` and `framework` submodules.
2. `pip install jinja2 jsonschema`, then run `scripts/make_generated_files.py`
   in the root and `framework/scripts/make_generated_files.py` in
   `tf-psa-crypto`.
3. Replace the directories listed above and the generated files.
4. Build both presets and make sure `win95check` still passes. New Windows
   API calls in a new release are exactly what it catches.
5. Update this file.
