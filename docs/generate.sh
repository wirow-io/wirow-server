#!/bin/sh

set -e
test -z "$1" && exit 1

DIR=$(
  cd "$(dirname "$0")"
  pwd -P
)
cd $DIR

asciidoctor-pdf \
  -a pdf-fontsdir=${HOME}/.fonts \
  $1
