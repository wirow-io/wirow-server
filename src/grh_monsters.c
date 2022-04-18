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

#include "grh_monsters.h"
#include "grh_monsters_data.h"
#include "utils/trandom.h"

#include <iowow/murmur3.h>
#include <iowow/iwp.h>

#include <stdlib.h>
#include <string.h>

static void _set_part(
  uint8_t part[MONSTER_PIXELS][MONSTER_PIXELS],
  uint8_t out[MONSTER_PIXELS][MONSTER_PIXELS]
  ) {
  for (int i = 0; i < MONSTER_PIXELS; ++i) {
    for (int j = 0; j < MONSTER_PIXELS; ++j) {
      uint8_t v = part[i][j];
      if (v == 1) {
        out[i][j] = 1;
      } else if (v == 2) {
        out[i][j] = 0;
      }
    }
  }
}

static int _handler(struct iwn_wf_req *req, void *user_data) {
  static uint32_t _seed;
  int ret = 500;

  uint8_t out[MONSTER_PIXELS][MONSTER_PIXELS] = { 0 };
  char color[8];

  const char *full_path = req->path_unmatched;
  uint32_t ul = murmur3(full_path, strlen(full_path));

  __sync_bool_compare_and_swap(&_seed, 0, rand());
  uint64_t seed = (uint64_t) ul | (uint64_t) _seed << 31;

  snprintf(color, sizeof(color), "#%.2x%.2x%.2x",
           (rand() % 201) + 55,
           (rand() % 201) + 55,
           (rand() % 201) + 55);

#define GEN_PART(part__)                                                  \
  _set_part(part__[lcg64_temper(&seed) % (sizeof(part__) / sizeof(part__[0]))], out)

  GEN_PART(legs);
  GEN_PART(hair);
  GEN_PART(arms);
  GEN_PART(body);
  GEN_PART(eyes);
  GEN_PART(month);

  iwrc rc = 0;
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);

  RCC(rc, finish,
      iwxstr_printf(xstr,
                    "<svg version='1.1' xmlns='http://www.w3.org/2000/svg'"
                    " viewBox='0 0 12 12'"
                    " style='fill:%s;'>\n",
                    color));

  for (int i = 0; i < MONSTER_PIXELS; ++i) {
    for (int j = 0; j < MONSTER_PIXELS; ++j) {
      if (out[i][j]) {
        RCC(rc, finish, iwxstr_printf(xstr, "<rect x='%d' y='%d' width='1.1' height='1.1' />", j, i));
      }
    }
  }
  iwxstr_cat2(xstr, "</svg>");
  ret = iwn_http_response_write(req->http, 200, "image/svg+xml", iwxstr_ptr(xstr), iwxstr_size(xstr)) ? 1 : -1;

#undef GEN_PART

finish:
  iwxstr_destroy(xstr);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return ret;
}

iwrc grh_route_monsters(struct iwn_wf_route *parent) {
  return iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/monster/",
    .flags = IWN_WF_MATCH_PREFIX,
    .handler = _handler
  }, 0);
}
