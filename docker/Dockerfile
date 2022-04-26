FROM alpine:latest AS build

RUN apk add --no-cache git make nodejs npm yarn musl-dev autoconf automake yasm \
                       clang py3-pip curl pkgconf cmake libtool binutils bash clang-dev gcc g++ \
                       diffutils patch linux-headers
RUN git clone --recurse-submodules https://github.com/wirow-io/wirow-server.git

RUN mkdir -p /wirow-server/build
WORKDIR /wirow-server/build

ENV CC=clang
ENV CFLAGS=-static
ENV CXX=clang++
ENV CXXFLAGS=-static
RUN cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DIW_EXEC=ON
    # -DSENTRY_DSN=https://...@.../... \
    # -DSENTRY_FRONT_DSN=https://...@.../... \
RUN make -j4

FROM alpine:latest

COPY --from=build /wirow-server/build/src/wirow /wirow
RUN mkdir /data /config
COPY ./wirow.ini /config

VOLUME /data /config

EXPOSE 80 443
EXPOSE 44300-44600/tcp
EXPOSE 44300-44600/udp

ENTRYPOINT ["/wirow", "-c", "/config/wirow.ini"]

