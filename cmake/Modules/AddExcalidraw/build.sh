#!/bin/sh
set -e
set -x

yarn --non-interactive --no-progress install --check-files
cd src/packages/excalidraw
yarn --non-interactive --no-progress install --check-files
yarn run build:umd:prod
mkdir -p $(dirname $1)
yarn pack --filename "$1"
