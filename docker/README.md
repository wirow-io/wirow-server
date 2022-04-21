## Building Docker Image

[Wirow server Dockerfile](https://github.com/wirow-io/wirow-server/blob/master/docker/Dockerfile)

```sh
cd ./docker
docker build -t wirow .
```

Please mind about the following volume dirs defined in [Dokerfile](https://github.com/wirow-io/wirow-server/blob/master/docker/Dockerfile):
- `/data` Where wirow database, uploads and room recordings are located.
- `/config/wirow.ini` A server configuration file.

Before starting Wirow docker container
- Read the [Wirow server Administrator's Guide](https://github.com/wirow-io/wirow-server/blob/master/docs/wirow.adoc)
- Review `/config/wirow.ini` ip/network options, ssl certs section (if you don't plan to use Let's Encrypt).

```sh
docker run wirow -h
```

```sh
docker run wirow -n mywirow.example.com
```
