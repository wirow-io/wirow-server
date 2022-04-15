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
