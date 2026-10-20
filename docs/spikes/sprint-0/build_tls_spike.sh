#!/bin/sh
# Builds the Sprint 0 TLS spike. Run it in a directory containing tls_spike.c
# and mbedtls-3.6.7-win95-config.h copied as tachyon_mbedtls_user_config.h,
# with M pointing at an mbedTLS v3.6.7 checkout. Test it against a local
# server: openssl s_server -accept 4433 -cert srv.pem -key srv.key -www,
# then: wine tls_spike.exe ca.pem 4433
set -e
M=${M:-/home/user/mbed-tls/mbedtls}
CC=i686-w64-mingw32-gcc
CF="-ffunction-sections -fdata-sections -O2 -march=i586 -std=c99 -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -I$M/include -I$M/library -I."
rm -rf obj && mkdir obj
for f in $M/library/*.c; do
  $CC -c $CF '-DMBEDTLS_USER_CONFIG_FILE="tachyon_mbedtls_user_config.h"' "$f" -o "obj/$(basename "$f" .c).o" &
  while [ "$(jobs -r | wc -l)" -ge 4 ]; do sleep 0.1; done
done
wait
rm -f libmbed95.a && i686-w64-mingw32-ar rcs libmbed95.a obj/*.o
$CC $CF '-DMBEDTLS_USER_CONFIG_FILE="tachyon_mbedtls_user_config.h"' tls_spike.c libmbed95.a -static -nodefaultlibs -Wl,--start-group -lmingw32 -lgcc -lgcc_eh -lmingwex -lcrtdll -lkernel32 -lwsock32 -Wl,--end-group -Wl,--gc-sections -o tls_spike.exe
