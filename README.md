# Wirow video conferencing platform

https://wirow.io

A full featured self-hosted video web-conferencing platform shipped as a single executable.

- Works on any Linux machine.
- Single executable, no setup is required.
- Let's Encrypt integration - instant SSL certs generation for your web-conferencing host.
- Unlimited meeting rooms and webinars.
- Integrated whiteboard.
- Video calls recording.
- Low memory/CPU consumption due to fast core engine written in C.

## Licensing

Wirow platform community edition is distributed under terms of [AGPLv3 license](https://choosealicense.com/licenses/agpl-3.0/)

For a license to use the Wirow software under conditions other than AGPLv3, or for technical support for this software,
please contact us at info@wirow.io

## Software used by Wirow

- [Mediasoup - C++ WebRTC SFU router](https://github.com/versatica/mediasoup)
- [EJDB2 - Embeddable JSON Database engine](https://github.com/Softmotions/ejdb)
- [IWNET - Asynchronous HTTP Library](https://github.com/Softmotions/iwnet)
- [Excalidraw - A Whiteboard Web UI](https://github.com/excalidraw/excalidraw)
- [FFmpeg](https://ffmpeg.org)
- [Sentry error reporting](https://sentry.io)
- [Svelte Frontend Framework](https://svelte.dev)

## Intro

[![Wirow intro](./docs/artwork/Screens/youtube.jpg)](https://www.youtube.com/watch?v=14-DI3lk_P0)

![](./docs/artwork/Screens/screen1.png)

![](./docs/artwork/Screens/screen2.png)

![](./docs/artwork/Screens/screen3.png)




## Building from sources

### Build Prerequisites

* Linux, macOS or FreeBSD
* Git
* CMake v3.18+
* GNU Make, autoconf, automake, ninja
* Nodejs v14+ and Yarn package manager v1.22+
* Clang C/C++ compiler v10+ or GCC v9+
* yasm assembler (yasm) needed by FFmpeg
* Python 3 pip package manager (python3-pip)

Example of prerequisite software installation on Debian/Ubuntu Linux:

```sh
apt-get install -y apt-utils software-properties-common \
                   apt-transport-https sudo curl wget gpg

wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
       | gpg --dearmor -i \
       | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
wget -qO- https://deb.nodesource.com/setup_lts.x | bash -
wget -qO- https://dl.yarnpkg.com/debian/pubkey.gpg | apt-key add -

apt-add-repository -y 'deb https://apt.kitware.com/ubuntu/ focal main'
echo "deb https://dl.yarnpkg.com/debian/ stable main" | tee /etc/apt/sources.list.d/yarn.list

apt-get update
apt-get install -y autoconf automake pkgconf \
  binutils build-essential ca-certificates cmake \
  g++ gcc git libtool make ninja-build nodejs python-is-python3 yarn \
  yasm python3-pip
```

### Building

```sh
git clone --recurse-submodules https://github.com/wirow-io/wirow-server.git

mkdir -p ./wirow-server/build && cd ./wirow-server/build

cmake ..  -G Ninja \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DIW_EXEC=ON
ninja
```

Wirow build artifacts are located here:

```sh
 ./build/src/wirow    # Stripped binary
 ./build/src/wirow_g  # Binary with debug symbols (Not stripped)
```





## Guides

[Wirow server Administrator's Guide](https://github.com/wirow-io/wirow-server/blob/master/docs/wirow.adoc) ([pdf](https://github.com/wirow-io/wirow-server/blob/master/docs/wirow.pdf))

# License

```
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
```
