#!/bin/sh
# Make the TLS test certificates in certs/ (committed; rerun only to change
# them). Test-only keys: never trust them anywhere else.
#
#   ca.pem         Tachyon Test CA (ECDSA P-256), valid 2026-2056
#   rsa.pem        localhost, RSA 2048, signed by the CA
#   ecdsa.pem      localhost, ECDSA P-256, signed by the CA
#   expired.pem    localhost, valid only in 2020
#   future.pem     localhost, not valid until 2045
#   wronghost.pem  wrong.example
#   selfsigned.pem localhost, signed by itself
set -eu
cd "$(dirname "$0")/certs"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
touch "$work/index.txt"
echo 1000 > "$work/serial"
cat > "$work/ca.cnf" <<CNF
[ ca ]
default_ca = test
[ test ]
database = $work/index.txt
serial = $work/serial
new_certs_dir = $work
default_md = sha256
policy = anything
copy_extensions = copy
unique_subject = no
[ anything ]
commonName = supplied
[ leaf ]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
CNF

ec_key() { openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$1" 2>/dev/null; }

ec_key ca.key
openssl req -x509 -new -key ca.key -sha256 -subj "/CN=Tachyon Test CA" -days 10957 \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out ca.pem

# leaf NAME KEYTYPE HOST START END
leaf() {
    if [ "$2" = rsa ]; then
        openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$1.key" 2>/dev/null
    else
        ec_key "$1.key"
    fi
    openssl req -new -key "$1.key" -subj "/CN=$3" \
        -addext "subjectAltName=DNS:$3,IP:127.0.0.1" -out "$work/$1.csr"
    openssl ca -batch -config "$work/ca.cnf" -cert ca.pem -keyfile ca.key -extensions leaf \
        -startdate "$4" -enddate "$5" -in "$work/$1.csr" -out "$1.pem" 2>/dev/null
}

leaf rsa rsa localhost 20260101000000Z 20460101000000Z
leaf ecdsa ec localhost 20260101000000Z 20460101000000Z
leaf expired ec localhost 20200101000000Z 20210101000000Z
leaf future ec localhost 20450101000000Z 20460101000000Z
leaf wronghost ec wrong.example 20260101000000Z 20460101000000Z

ec_key selfsigned.key
openssl req -x509 -new -key selfsigned.key -sha256 -subj "/CN=localhost" -days 7300 \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" -out selfsigned.pem
