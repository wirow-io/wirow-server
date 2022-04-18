#pragma once
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
