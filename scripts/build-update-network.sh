#!/usr/bin/env bash
set -euo pipefail
: "${VITASDK:?Set VITASDK}"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$root/build/update-network"
prefix="$work/install"
stamp="$(cat "$0" "$root/vendor/update-network/mbedtls-vita.patch" | sha256sum | cut -d' ' -f1)"
if [[ -f "$prefix/complete" && "$(cat "$prefix/complete")" == "$stamp" && -f "$prefix/lib/libcurl.a" ]]; then exit 0; fi
mkdir -p "$work/downloads" "$work/source"
fetch() {
  local name="$1" sha="$2" url="$3"
  if [[ ! -f "$work/downloads/$name" ]]; then
    curl --fail --location --connect-timeout 15 --max-time 180 --retry 2 "$url" -o "$work/downloads/$name.part"
    printf '%s  %s\n' "$sha" "$work/downloads/$name.part" | sha256sum --check
    mv "$work/downloads/$name.part" "$work/downloads/$name"
  fi
  printf '%s  %s\n' "$sha" "$work/downloads/$name" | sha256sum --check
}
fetch mbedtls.tar.bz2 4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8 \
  https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2
fetch curl.tar.xz f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7 \
  https://github.com/curl/curl/releases/download/curl-8_22_0/curl-8.22.0.tar.xz
tar -xjf "$work/downloads/mbedtls.tar.bz2" -C "$work/source"
tar -xJf "$work/downloads/curl.tar.xz" -C "$work/source"
mbed="$work/source/mbedtls-3.6.5"
patch -d "$mbed" -p1 < "$root/vendor/update-network/mbedtls-vita.patch"
python3 "$mbed/scripts/config.py" -f "$mbed/include/mbedtls/mbedtls_config.h" set MBEDTLS_THREADING_C
python3 "$mbed/scripts/config.py" -f "$mbed/include/mbedtls/mbedtls_config.h" set MBEDTLS_THREADING_PTHREAD
cmake -S "$mbed" -B "$work/mbedtls" -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DMBEDTLS_FATAL_WARNINGS=OFF
cmake --build "$work/mbedtls" -j4
cmake --install "$work/mbedtls"
cmake -S "$work/source/curl-8.22.0" -B "$work/curl" -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DENABLE_IPV6=OFF \
  -DCURL_DISABLE_SOCKETPAIR=ON -DHAVE_FCNTL_O_NONBLOCK=OFF -DENABLE_THREADED_RESOLVER=OFF \
  -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
  -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_LIBPSL=OFF \
  -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DCURL_USE_GSSAPI=OFF \
  -DCURL_ZLIB=OFF -DCURL_ZSTD=OFF -DCURL_BROTLI=OFF -DUSE_LIBIDN2=OFF \
  -DHTTP_ONLY=ON -DHAVE_PIPE2=0 -DCMAKE_DISABLE_FIND_PACKAGE_Threads=ON
cmake --build "$work/curl" -j4
cmake --install "$work/curl"
mkdir -p "$prefix/licenses"
cp "$mbed/LICENSE" "$prefix/licenses/MBEDTLS.txt"
cp "$work/source/curl-8.22.0/COPYING" "$prefix/licenses/CURL.txt"
printf '%s' "$stamp" > "$prefix/complete"
