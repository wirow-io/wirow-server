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

#include "urandom.h"

#include <ejdb2/iowow/iwlog.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

iwrc urandom(void *out_buf, size_t nbytes) {
  ssize_t rb;
  iwrc rc = 0;
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd == -1) {
    return iwrc_set_errno(IW_ERROR_ERRNO, errno);
  }
again:
  rb = read(fd, out_buf, nbytes);
  if (rb == -1) {
    if (errno == EINTR) {
      goto again;
    }
    rc = iwrc_set_errno(IW_ERROR_IO_ERRNO, errno);
  } else if (rb != nbytes) {
    rc = IW_ERROR_FAIL;
  }
  close(fd);
  return rc;
}
