## Building Docker Image

[Wirow server Dockerfile](https://github.com/wirow-io/wirow-server/blob/master/docker/Dockerfile)

```sh
cd ./docker
docker build --no-cache --force-rm -t wirow .
```

Please mind about the following volume dirs defined in [Dokerfile](https://github.com/wirow-io/wirow-server/blob/master/docker/Dockerfile):
- `/data` Where wirow database, uploads and room recordings are located.
- `/config/wirow.ini` A server configuration file.

Before starting Wirow docker container
- Read the [Wirow server Administrator's Guide](https://github.com/wirow-io/wirow-server/blob/master/docs/wirow.adoc)
- Review `/config/wirow.ini` ip/network options, ssl certs section (if you don't plan to use Let's Encrypt).

**Basic check of Wirow server availability:**

```sh
docker run --rm wirow -h
```

**Now you have three ways how to run a Wirow server:**
- Wirow server on your domain with Let's Encrypt HTTPS Certificates. (Recommended)
- Wirow server on your host with own HTTPS certificates.
- Wirow server behind HTTP proxy.

### Wirow server on your domain with Let's Encrypt Certificates (Recommended)

Please check what your host is accessible by your domain name and 80/443, 44300-44600 tcp/udp ports are accessible.

Example:

```sh

# Below the `start01` is the initial password for `admin` user

docker run --name wirow wirow -n mywirow.example.com -a start01
```

### Wirow server on your host with own HTTPS certificates

Here is the Example how it could be done:

```sh

WIROWSRC=<path to wirow cloned repo>
WORKDIR=<path to my workdir>

mkdir -p ${WORKDIR}/config
cd ${WORKDIR}/config

openssl req -x509 -nodes -days 3650 -newkey ec:<(openssl ecparam -name prime256v1) \
          -keyout wirow-eckey.pem -out wirow-ecdsacert.pem

cp ${WIROWSRC}/docker/wirow.ini ./wirow.ini

echo -e '\n\n[main]' >> ./wirow.ini
echo 'cert_file = {config_file_dir}/wirow-ecdsacert.pem' >> ./wirow.ini
echo 'cert_key_file = {config_file_dir}/wirow-eckey.pem' >> ./wirow.ini

# Below the `start01` is the initial password for admin user

docker run --name wirow -v ${WORKDIR}/config:/config wirow -a start01

23 Apr 17:15:06.543 INFO: main:ip=auto
23 Apr 17:15:06.543 INFO: main:data=/data
23 Apr 17:15:06.543 INFO: rtc:ports=44300..44600
23 Apr 17:15:06.543 INFO: main:cert_file=/config/wirow-ecdsacert.pem
23 Apr 17:15:06.543 INFO: main:cert_key_file=/config/wirow-eckey.pem
23 Apr 17:15:06.543 INFO: Number of workers: 6
23 Apr 17:15:06.544 INFO: Autodetected external IP address for wirow server: 172.17.0.2
23 Apr 17:15:06.544 INFO: Opening /data/wirow.db
23 Apr 17:15:06.791 INFO: Server on: 172.17.0.2:443
23 Apr 17:15:06.791 INFO: HTTP/HTTPS redirect port: 80
```

Now open your browser `https://172.17.0.2` (specific to this example) then enter `admin/start01` credentials at login page.
Later you may change your password and manage users by Admin UI.

### Wirow server behind HTTP proxy

Please refer to [Wirow server Administrator's Guide](https://github.com/wirow-io/wirow-server/blob/master/docs/wirow.adoc)


