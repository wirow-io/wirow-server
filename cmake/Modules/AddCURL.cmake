include(ExternalProject)

set(CURL_SOURCE_DIR ${CMAKE_SOURCE_DIR}/extra/curl)
set(LIBCURL ${CMAKE_BINARY_DIR}/lib/libcurl.a)
set(CMAKE_ARGS
    -DBUILD_CURL_EXE=OFF
    -DBUILD_SHARED_LIBS=OFF
    -DBearSSL_ROOT=${CMAKE_BINARY_DIR}
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
    -DHTTP_ONLY=ON
    -DCURL_USE_LIBSSH2=OFF
    -DCURL_USE_OPENSSL=OFF
    -DCURL_USE_BEARSSL=ON
    -DCURL_CA_BUNDLE=/dev/null/fake
    -DCURL_CA_PATH=none
    -DCURL_DISABLE_ALTSVC=ON
    -DCURL_DISABLE_COOKIES=ON
    -DCURL_DISABLE_CRYPTO_AUTH=ON
    -DCURL_WITHOUT_ZLIB=ON
    -DENABLE_IPV6=OFF
    -DENABLE_UNIX_SOCKETS=OFF
    -DHTTP_ONLY=ON
    -DUSE_LIBIDN2=OFF)

if(DEFINED CMAKE_TOOLCHAIN_FILE)
  list(APPEND CMAKE_ARGS "-DFIND_ROOT=${CMAKE_BINARY_DIR}")
endif()

foreach(extra CMAKE_C_COMPILER CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ${extra})
    list(APPEND CMAKE_ARGS "-D${extra}=${${extra}}")
  endif()
endforeach()

message("CURL CMAKE_ARGS: ${CMAKE_ARGS}")

ExternalProject_Add(
  extern_curl
  DEPENDS extern_ejdb2
  PREFIX ${CMAKE_BINARY_DIR}
  SOURCE_DIR ${CURL_SOURCE_DIR}
  BUILD_IN_SOURCE OFF
  CMAKE_ARGS ${CMAKE_ARGS}
  BUILD_BYPRODUCTS ${LIBCURL})

add_library(curl_s STATIC IMPORTED GLOBAL)
add_dependencies(curl_s iwnet_s extern_curl)
set_target_properties(curl_s PROPERTIES IMPORTED_LOCATION ${LIBCURL})
# set_target_properties(curl_s PROPERTIES INTERFACE_LINK_LIBRARIES "iwnet_s;dl")

# Minimal configure options for classic make
#
# ./configure \ --prefix= \ --with-bearssl= \ --with-default-ssl-backend=bearssl
# \ --disable-debug \ --enable-optimize \ --without-ssl \ --disable-alt-svc \
# --disable-ares \ --disable-cookies \ --disable-crypto-auth \ --disable-dict \
# --disable-doh \ --disable-ech \ --disable-file \ --disable-ftp \
# --disable-gopher \ --disable-hsts \ --disable-imap \ --disable-ipv6 \
# --disable-ldap \ --disable-ldaps \ --disable-manual \ --disable-mqtt \
# --disable-netrc \ --disable-ntlm-wb \ --disable-pop3 \
# --disable-progress-meter \ --disable-pthreads \ --disable-rt \ --disable-rtsp
# \ --disable-shared  \ --disable-silent-rules \ --disable-smb \ --disable-smtp
# \ --disable-socketpair \ --disable-sspi \ --disable-telnet \ --disable-tftp \
# --disable-threaded-resolver \ --disable-tls-srp \ --disable-unix-sockets \
# --disable-versioned-symbols \ --without-brotli \ --without-libidn2 \
# --without-librtmp \ --without-zlib
