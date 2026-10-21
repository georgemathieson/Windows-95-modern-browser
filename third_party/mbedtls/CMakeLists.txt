# Mbed TLS 4.1 LTS with TF-PSA-Crypto, built with Tachyon's configuration
# (tachyon/). See README.tachyon.md for the version and local changes.

file(GLOB TACHYON_MBEDTLS_SOURCES CONFIGURE_DEPENDS
  library/*.c
  tf-psa-crypto/core/*.c
  tf-psa-crypto/drivers/builtin/src/*.c
  tf-psa-crypto/platform/*.c
  tf-psa-crypto/utilities/*.c
  tf-psa-crypto/extras/*.c
)

add_library(mbedtls STATIC ${TACHYON_MBEDTLS_SOURCES})
target_include_directories(mbedtls SYSTEM PUBLIC
  include
  tf-psa-crypto/include
  tf-psa-crypto/drivers/builtin/include
  tachyon
)
target_include_directories(mbedtls PRIVATE
  library
  tf-psa-crypto/core
  tf-psa-crypto/dispatch
  tf-psa-crypto/drivers/builtin/src
  tf-psa-crypto/extras
  tf-psa-crypto/platform
  tf-psa-crypto/utilities
)
target_compile_definitions(mbedtls PUBLIC
  MBEDTLS_USER_CONFIG_FILE="tachyon_mbedtls_config.h"
  TF_PSA_CRYPTO_USER_CONFIG_FILE="tachyon_crypto_config.h"
)
# Upstream code: Tachyon's warning flags (and -Werror) don't apply.
target_compile_options(mbedtls PRIVATE -w)
