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

#include "network.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <ejdb2/iowow/iwlog.h>

static int _ifa_score(struct ifaddrs *ifa) {
  if (  !ifa->ifa_addr
     || ((ifa->ifa_addr->sa_family != AF_INET) && (ifa->ifa_addr->sa_family != AF_INET6))) {
    return 0;
  }
  const char *name = ifa->ifa_name;
  if (name) {
    if ((strncmp(name, "eth", 3) == 0) || (strncmp(name, "en", 2) == 0)) {
      return ifa->ifa_addr->sa_family == AF_INET ? 3 : 2;
    } else if (strncmp(name, "wl", 2) == 0) {
      return ifa->ifa_addr->sa_family == AF_INET ? 2 : 1;
    }
  }
  return 0;
}

char* network_detect_best_listen_ip_address(void) {
  iwrc rc = 0;
  char host[NI_MAXHOST];
  struct ifaddrs *ifaddr, *ifab = 0;
  int score = 0;
  char *ret = 0;

  if (getifaddrs(&ifaddr) == -1) {
    rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
    goto finish;
  }
  for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    int ns = _ifa_score(ifa);
    if (ns > score) {
      ifab = ifa;
      score = ns;
    }
  }
  if (ifab) {
    if (getnameinfo(ifab->ifa_addr, sizeof(struct sockaddr_in),
                    host, NI_MAXHOST, 0, 0, NI_NUMERICHOST)) {
      rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
      goto finish;
    }
    ret = strdup(host);
  }

finish:
  if (ifaddr) {
    freeifaddrs(ifaddr);
  }
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return ret;
}

uint16_t network_detect_unused_port(const char *ip) {
  socklen_t len;
  uint16_t port = 0;
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd == -1) {
    return 0;
  }
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
  };
  if (ip) {
    if (!inet_aton(ip, &addr.sin_addr)) {
      goto finish;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int[]) { 1 }, sizeof(int)) == -1) {
      goto finish;
    }
  }
  if (bind(fd, (void*) &addr, sizeof(addr)) == -1) {
    goto finish;
  }
  memset(&addr, 0, sizeof(addr));
  len = sizeof(addr);
  if (getsockname(fd, (void*) &addr, &len) == -1) {
    goto finish;
  }
  port = addr.sin_port;

finish:
  close(fd);
  return port;
}
