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

#include "lic_env.h"

#if ENABLE_MEDIASOUP == 1

#include <stdlib.h>
#include <stdint.h>

int mediasoup_worker_run(
  int argc,
  char *argv[],
  const char *version,
  int consumerChannelFd,
  int producerChannelFd,
  int payloadConsumeChannelFd,
  int payloadProduceChannelFd,
  void (*)(uint8_t**, uint32_t*, size_t*, const void*, void*),
  void*,
  void (*)(const uint8_t*, uint32_t, void*),
  void*,
  void (*)(uint8_t**, uint32_t*, size_t*, uint8_t**, uint32_t*, size_t*, void*, void*),
  void*,
  void (*)(const uint8_t*, uint32_t, const uint8_t*, uint32_t, void*),
  void*);

int exec_worker(int argc, char **argv) {
  return mediasoup_worker_run(argc, argv, "3.9.9", 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0);
}

#endif
