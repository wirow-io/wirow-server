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

#include <iowow/iwlog.h>
#include <iowow/iwjson.h>

#include <stdbool.h>

typedef enum {
  PROFILE_CONSTRAINED_BASELINE = 1,
  PROFILE_BASELINE,
  PROFILE_MAIN,
  PROFILE_CONSTRAINED_HIGH,
  PROFILE_HIGH,
} h264_profile_e;

typedef enum {
  LEVEL_1B  = 0,
  LEVEL_1   = 10,
  LEVEL_1_1 = 11,
  LEVEL_1_2 = 12,
  LEVEL_1_3 = 13,
  LEVEL_2   = 20,
  LEVEL_2_1 = 21,
  LEVEL_2_2 = 22,
  LEVEL_3   = 30,
  LEVEL_3_1 = 31,
  LEVEL_3_2 = 32,
  LEVEL_4   = 40,
  LEVEL_4_1 = 41,
  LEVEL_4_2 = 42,
  LEVEL_5   = 50,
  LEVEL_5_1 = 51,
  LEVEL_5_2 = 52,
  LEVEL_5_3 = 53,
} h264_level_e;

typedef struct h264_plid {
  h264_profile_e profile;
  h264_level_e   level;
} h264_plid_t;

iwrc h264_plid_parse(const char *spec, h264_plid_t *out);

bool h264_plid_equal(const char *p1, size_t p1len, const char *p2, size_t p2len);

iwrc h256_plid_to_answer(JBL_NODE local_params, JBL_NODE remote_params, h264_plid_t *out);

iwrc h256_plid_write(const h264_plid_t *plid, char buf[static 6]);
