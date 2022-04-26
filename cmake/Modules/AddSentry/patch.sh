#!/bin/sh
set -x -e
BASEDIR=$(dirname "$0")
patch -u external/third_party/lss/linux_syscall_support.h -i ${BASEDIR}/lss.patch
patch -u src/backends/sentry_backend_breakpad.cpp -i ${BASEDIR}/minidump.patch
patch -u -p1 -i ${BASEDIR}/libcurl_blob.patch
