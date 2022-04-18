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

#include <iowow/iwuuid.h>

#include <stdio.h>

#define FILES_UUID_DIR_BUFSZ (IW_UUID_STR_LEN + 1 + 6)

/// Convert uuid into the following form: `${uuid.substring(0,2)}/${uuid.substring(2,4)}/${uuid}`.
static void files_uuid_dir_copy(const char uuid[static IW_UUID_STR_LEN + 1], char out_dir[static FILES_UUID_DIR_BUFSZ]) {
  snprintf(out_dir, FILES_UUID_DIR_BUFSZ, "%.2s/%.2s/%s", uuid, uuid + 2, uuid);
}
