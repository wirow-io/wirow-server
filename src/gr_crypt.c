#include "gr_crypt.h"

#include <iowow/iwutils.h>
#include <iowow/iwlog.h>
#include <iowow/iwconv.h>
#include <iwnet/bearssl_hash.h>
#include <iwnet/iwnet.h>
#include <curl/curl.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/limits.h>

int64_t gr_crypt_pwhash(const void *input, size_t len, int64_t salt, char hash[65]) {
  uint64_t seed = salt;
  while (!seed) {
    seed = (iwu_rand_u32() << 31) | iwu_rand_u32();
  }
  char *hbuf = malloc(len + sizeof(seed));
  if (!hbuf) {
    return 0;
  }
  memcpy(hbuf, input, len);
  memcpy(hbuf + len, &seed, sizeof(seed));

  uint8_t hashdata[br_sha256_SIZE];
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, hbuf, len + sizeof(seed));
  br_sha256_out(&ctx, hashdata);
  iwbin2hex(hash, 65, hashdata, sizeof(hashdata));

  free(hbuf);
  return (int64_t) seed;
}

void gr_crypt_sha256(const void *input, size_t len, char hash[65]) {
  uint8_t hashdata[br_sha256_SIZE];
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, input, len);
  br_sha256_out(&ctx, hashdata);
  iwbin2hex(hash, 65, hashdata, sizeof(hashdata));
}
