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

#include "grh.h"
#include <stdint.h>
#include <string.h>

extern struct gr_env g_env;

struct resource {
  const char *path;
  const char *ctype;
  const unsigned char *data;
  size_t   len;
  uint32_t hash;
  bool     gz;
  bool     cache;
};

/* Python code for easy calculation:
   def _hash(s):
    hash = 5381
    for i in s: hash = hash * 33 + ord(i)
    return hash % (1 << 32)
 */
IW_INLINE uint32_t _hash(const char *str) {
  unsigned char c;
  uint32_t hash = 5381;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

#define RES(path_, hash_, ctype_, name_, gz_, cache_) \
  { .path = (path_), .hash = (hash_), .ctype = (ctype_), .data = (name_), .len = name_ ## _len, .gz = (gz_), \
    .cache = (cache_) }

static int _serve(struct resource *r, struct iwn_wf_req *req) {
  if (r->cache) {
    iwn_http_response_header_set(req->http, "cache-control", "max-age=604800, immutable",
                                 IW_LLEN("max-age=604800, immutable"));
  } else if (!g_env.private_overlays.watch) {
    iwn_http_response_header_set(req->http, "cache-control", "max-age=300", IW_LLEN("max-age=300"));
  }
  if (r->gz) {
    iwn_http_response_header_set(req->http, "content-encoding", "gzip", IW_LLEN("gzip"));
  }
  return iwn_http_response_write(req->http, 200, r->ctype, (const char*) r->data, r->len) ? 1 : -1;
}
