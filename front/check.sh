#!/bin/sh

set -e
SCRIPTPATH="$(
  cd "$(dirname "$0")"
  pwd -P
)"

cd ${SCRIPTPATH}
./node_modules/.bin/svelte-check --compiler-warnings unused-export-let:ignore
