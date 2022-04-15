#pragma once

#include <stdint.h>

static int32_t temper(int32_t x) {
  x ^= x >> 11;
  x ^= x << 7 & 0x9D2C5680;
  x ^= x << 15 & 0xEFC60000;
  x ^= x >> 18;
  return x;
}

static int32_t lcg64_temper(uint64_t *seed) {
  *seed = 6364136223846793005ULL * *seed + 1;
  return temper(*seed >> 32);
}
