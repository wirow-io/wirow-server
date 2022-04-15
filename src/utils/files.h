#pragma once

#include <iowow/iwuuid.h>

#include <stdio.h>

#define FILES_UUID_DIR_BUFSZ (IW_UUID_STR_LEN + 1 + 6)

/// Convert uuid into the following form: `${uuid.substring(0,2)}/${uuid.substring(2,4)}/${uuid}`.
static void files_uuid_dir_copy(const char uuid[static IW_UUID_STR_LEN + 1], char out_dir[static FILES_UUID_DIR_BUFSZ]) {
  snprintf(out_dir, FILES_UUID_DIR_BUFSZ, "%.2s/%.2s/%s", uuid, uuid + 2, uuid);
}
