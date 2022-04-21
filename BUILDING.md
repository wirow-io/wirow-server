## Building Docker Image

[Wirow server Dockerfile](./docker/Dockerfile)

```sh
cd ./docker
docker build -t wirow .
```

Please mind about the following volume dirs defined in [Dokerfile](./docker/Dockerfile):
- `/data` Where wirow database, uploads, room recording located.
- `/config/wirow.ini` configuration used by server.

Before starting Wirow docker container
- Read [Wirow server Administrator's Guide](./docs/wirow.adoc)
- Review `/config/wirow.ini` ip/network options, ssl certs section (if you don't plat to use Let's Encrypt).

```sh
docker run wirow -h
```

```sh
docker run wirow -n mywirow.example.com
```

## Building from sources by hands

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


