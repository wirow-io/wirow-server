#!/bin/bash

set -e
# set -x

SCRIPTPATH="$(
  cd "$(dirname "$0")"
  pwd -P
)"
cd $SCRIPTPATH

readme() {
  echo "Generating README.md";
  cat "./INFO.md" > "./README.md"
  echo -e "\n\n" >> "./README.md"
  cat "./docker/README.md" >> "./README.md"
  echo -e "\n\n" >> "./README.md"
  cat "./BUILDING.md" >> "./README.md"
  echo -e "\n\n" >> "./README.md"
  cat "./docs/README.md" >> "./README.md"
  cat <<EOF >> "./README.md"
# License

\`\`\`
/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */
\`\`\`
EOF
}

while [ "$1" != "" ]; do
  case $1 in
    "-d"  )  readme
             exit
             ;;
  esac
  shift
done
