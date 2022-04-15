#ifndef __NETSTRING_STREAM_H
#define __NETSTRING_STREAM_H

#include <string.h>
#include <ejdb2/iowow/iwpool.h>

int netstring_read(
  const char *buffer, size_t buffer_length,
  const char **netstring_start, size_t *netstring_length);

size_t netstring_buffer_size(size_t data_length);

size_t netstring_encode_new(char **netstring, const char *data, size_t len);

size_t netstring_encode_pool(char **netstring, const char *data, size_t len, IWPOOL *pool);

/* Errors that can occur during netstring parsing */
#define NETSTRING_ERROR_TOO_LONG     -1
#define NETSTRING_ERROR_NO_COLON     -2
#define NETSTRING_ERROR_TOO_SHORT    -3
#define NETSTRING_ERROR_NO_COMMA     -4
#define NETSTRING_ERROR_LEADING_ZERO -5
#define NETSTRING_ERROR_NO_LENGTH    -6

#endif
