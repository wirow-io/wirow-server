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

#include "gr.h"
#include "upd.h"
#include "lic_env.h"
#include "lic_vars.h"
#include "wrc/wrc.h"
#include "gr_crypt.h"
#include "utils/xcurl.h"

#include <iowow/iwjson.h>
#include <iwnet/iwnet.h>
#include <iwnet/bearssl_hash.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>

#ifdef LICENSE_ID

// Pthread return code check
#define THCG(rc_, label_, op_)                            \
  {                                                       \
    int err_ = op_;                                         \
    if (err_) {                                             \
      rc_ = iwrc_set_errno(IW_ERROR_THREADING_ERRNO, err_); \
      goto label_;                                          \
    }                                                       \
  }

// Syscall return code check
#define LCG(rc_, label_, op_)                    \
  if (op_ == -1) {                               \
    rc_ = iwrc_set_errno(IW_ERROR_ERRNO, errno); \
    goto label_;                                 \
  }

extern struct gr_env g_env;

// Bool that shows if update is running now
static atomic_int _updating = 0;
// Bool that shows if update need to be applied
static atomic_bool _pending = false;

static char _hash[65] = { 0 };

static iwrc _upd_download_update(const char *url, char *path) {
  iwrc rc = 0;
  CURLcode cc = 0;
  CURL *curl = 0;
  FILE *fout = 0;
  struct curl_slist *headers = 0;

  curl = curl_easy_init();
  if (!curl) {
    rc = GR_ERROR_CURL_API_FAIL;
    goto finish;
  }

  fout = fopen(path, "wb");
  if (!fout) {
    rc = iwrc_set_errno(GR_ERROR_CURL_API_FAIL, errno);
    goto finish;
  }

  curl_easy_reset(curl);

  struct curl_blob cainfo_blob = {
    .data  = (void*) iwn_cacerts,
    .len   = iwn_cacerts_len,
    .flags = CURL_BLOB_NOCOPY,
  };

  headers = curl_slist_append(curl_slist_append(0, "Expect:"), "X-License-Id: " LICENSE_ID);
  RCA(headers, finish);

  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_URL, url));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &cainfo_blob));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEDATA, fout));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, LICENSE_SSL_VERIFY));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, LICENSE_SSL_VERIFY));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1));

  XCC(cc, finish, curl_easy_perform(curl));

  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  if (code != 200) {
    iwlog_warn("Downloading update failed with code %ld", code);
    rc = GR_ERROR_CURL_API_FAIL;
    goto finish;
  }

finish:
  if (headers) {
    curl_slist_free_all(headers);
  }
  if (fout) {
    fclose(fout);
  }
  if (curl) {
    curl_easy_cleanup(curl);
  }
  if (rc) {
    remove(path);
  }
  return rc;
}

static iwrc _upd_get_file_hash(const char *file, char *hash) {
  iwrc rc = 0;

  br_sha256_context ctx;
  br_sha256_init(&ctx);

  FILE *fin = fopen(file, "rb");
  if (fin == 0) {
    rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
    goto finish;
  }

  while (true) {
    uint8_t buf[8192];
    size_t n = fread(buf, 1, 8192, fin);
    br_sha256_update(&ctx, buf, n);
    if (n != 8192) {
      break;
    }
  }

  uint8_t hashdata[32];
  br_sha256_out(&ctx, hashdata);
  for (int i = 0; i < 32; i++) {
    sprintf(hash + i * 2, "%02x", hashdata[i]);
  }

finish:
  if (fin) {
    fclose(fin);
  }
  return rc;
}

static iwrc _upd_verify_hash(const char *file, const char *hash) {
  iwrc rc = 0;
  char check[65];
  RCC(rc, finish, _upd_get_file_hash(file, check));
  if (strcmp(hash, check) != 0) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }
finish:
  return rc;
}

static iwrc _upd_copy_file(const char *from, const char *to) {
  iwrc rc = 0;
  FILE *fin, *fout = 0;

  fin = fopen(from, "rb");
  if (fin == 0) {
    rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
    goto finish;
  }

  fout = fopen(to, "wb");
  if (fout == 0) {
    rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
    goto finish;
  }

  while (true) {
    uint8_t buf[8192];
    size_t nin = fread(buf, 1, 8192, fin);
    size_t nout = fwrite(buf, 1, nin, fout);
    if (nout != nin) {
      rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
      goto finish;
    }
    if (nin != 8192) {
      break;
    }
  }

finish:
  if (fin) {
    fclose(fin);
  }
  if (fout) {
    fclose(fout);
  }
  return rc;
}

static iwrc _wrc_event_handler(wrc_event_e evt, wrc_resource_t resource_id, JBL data, void *op) {
  return 0;
  /*
     if (evt != WRC_EVT_DEEP_IDLE_STARTED) {
     return 0;
     }
     bool locked = false;
     iwrc rc = 0;

     if (++_updating == 1) {
     locked = true;

     iwlog_info("Deep idle detected, applying update and shutting down");
     LCG(rc, finish, raise(SIGTERM));
     } else {
     _updating--;
     goto finish;
     }

     finish:
     if (locked) {
     _updating--;
     }

     return rc;
   */
}

static void* _upd_thread_func(void *_param) {
  IWXSTR *upd_dir = 0, *upd_tmp_path = 0, *upd_path = 0;
  const char *url = _param;
  const char *hash = url + strlen(url) + 1;
  bool registered = false;
  iwrc rc = 0;

  RCC(rc, finish, gr_app_thread_register(pthread_self()));
  registered = true;

  iwlog_info("New update found, downloading...");

  RCA((upd_dir = iwxstr_new()), finish);
  RCA((upd_tmp_path = iwxstr_new()), finish);
  RCA((upd_path = iwxstr_new()), finish);

  RCC(rc, finish, iwxstr_printf(upd_dir, "%s/updates/", g_env.data_dir));
  RCC(rc, finish, iwxstr_printf(upd_tmp_path, "%s/downloading.tmp", iwxstr_ptr(upd_dir)));
  RCC(rc, finish, iwxstr_printf(upd_path, "%s/pending", iwxstr_ptr(upd_dir)));

  RCC(rc, finish, iwp_mkdirs(iwxstr_ptr(upd_dir)));

  // Check that current update file is not up to date (in case of restart, when update was not applied)
  if (_upd_verify_hash(iwxstr_ptr(upd_path), hash) != 0) {
    RCC(rc, finish, _upd_download_update(url, iwxstr_ptr(upd_tmp_path)));
    RCC(rc, finish, _upd_verify_hash(iwxstr_ptr(upd_tmp_path), hash));
    LCG(rc, finish, rename(iwxstr_ptr(upd_tmp_path), iwxstr_ptr(upd_path)));
    strcpy(_hash, hash);
  }

  _pending = true;
  /*
     if (wrc_check_deep_idle()) {
     iwlog_info("Update has been downloaded, now applying & shutting down in hope of restart");
     LCG(rc, finish, raise(SIGTERM));
     } else {
     iwlog_info("Update has been downloaded, now waiting for deep idle to apply");
     if (_evt == 0) {
      RCC(rc, finish, wrc_add_event_handler(_wrc_event_handler, 0, &_evt));
     }
     }
   */

finish:
  free(_param);
  iwxstr_destroy(upd_dir);
  iwxstr_destroy(upd_tmp_path);
  iwxstr_destroy(upd_path);

  if (rc) {
    iwlog_ecode_error(rc, "Failed to update, waiting for the next update check");
  }
  if (registered) {
    gr_app_thread_unregister(pthread_self());
  }
  _updating--;
  pthread_detach(pthread_self());
  return 0;
}

iwrc gr_upd_process_license(const char *resp) {
  JBL_NODE n, n_url, n_hash;
  char *param = 0;
  bool locked = false, started = false;
  pthread_t _upd_thread = 0;

  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  iwrc rc = jbn_from_json(resp, &n, pool);
  RCGO(rc, finish);
  rc = jbn_at(n, "/upd", &n_url);
  if (rc == JBL_ERROR_PATH_NOTFOUND || n_url->type == JBV_NONE || n_url->type == JBV_NULL) {
    rc = 0;
    goto finish;
  }
  RCGO(rc, finish);
  if (n_url->type != JBV_STR) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }
  rc = jbn_at(n, "/updsum", &n_hash);
  if (rc == JBL_ERROR_PATH_NOTFOUND || n_hash->type != JBV_STR) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }
  RCGO(rc, finish);

  if (_hash[0] == 0) {
    char hash[65];
    RCC(rc, finish, _upd_get_file_hash(g_env.program, hash));
    strcpy(_hash, hash);
  }

  param = malloc((n_url->vsize + n_hash->vsize + 2) * sizeof(char));
  if (!param) {
    rc = iwrc_set_errno(IW_ERROR_ALLOC, errno);
    goto finish;
  }
  memcpy(param, n_url->vptr, n_url->vsize);
  param[n_url->vsize] = 0;
  memcpy(param + n_url->vsize + 1, n_hash->vptr, n_hash->vsize);
  param[n_url->vsize + 1 + n_hash->vsize] = 0;

  if (++_updating == 1) {
    locked = true;

    if (strcmp(_hash, n_hash->vptr) == 0) {
      goto finish;
    }

    THCG(rc, finish, pthread_create(&_upd_thread, 0, _upd_thread_func, param));

    started = true;
  } else {
    _updating--;
    goto finish;
  }

finish:
  if (locked && !started) {
    _updating--;
  }
  if (!started) { // Case when thread was not created
    free(param);
  }
  iwpool_destroy(pool);
  return rc;
}

iwrc gr_upd_apply() {
  if (!_pending) {
    return 0;
  }

  iwrc rc = 0;
  struct stat selfstat;
  bool backup = false;
  IWXSTR *upd_path = 0, *backup_dir = 0, *backup_dir_tmp = 0, *backup_bin_path = 0, *backup_db_path = 0;

  const char *db_filename = strrchr(g_env.dbparams.path, '/');
  if (db_filename == 0) {
    db_filename = g_env.dbparams.path;
  } else {
    db_filename++;
  }

  const char *program_filename = strrchr(g_env.program, '/');
  if (program_filename == 0) {
    program_filename = g_env.program;
  } else {
    program_filename++;
  }

  iwlog_info("Creating backup before applying update");

  RCA((upd_path = iwxstr_new()), finish);
  RCA((backup_dir = iwxstr_new()), finish);
  RCA((backup_dir_tmp = iwxstr_new()), finish);
  RCA((backup_bin_path = iwxstr_new()), finish);
  RCA((backup_db_path = iwxstr_new()), finish);

  RCC(rc, finish, iwxstr_printf(upd_path, "%s/updates/pending", g_env.data_dir));
  RCC(rc, finish, iwxstr_printf(backup_dir, "%s/backups/upd_%lu", g_env.data_dir, time(0)));
  RCC(rc, finish, iwxstr_printf(backup_dir_tmp, "%s.tmp", iwxstr_ptr(backup_dir)));
  RCC(rc, finish, iwxstr_printf(backup_bin_path, "%s/%s", iwxstr_ptr(backup_dir_tmp), program_filename));
  RCC(rc, finish, iwxstr_printf(backup_db_path, "%s/%s", iwxstr_ptr(backup_dir_tmp), db_filename));

  RCC(rc, finish, iwp_mkdirs(iwxstr_ptr(backup_dir_tmp)));

  RCC(rc, finish, _upd_copy_file(g_env.program, iwxstr_ptr(backup_bin_path)));
  RCC(rc, finish, _upd_copy_file(g_env.dbparams.path, iwxstr_ptr(backup_db_path)));

  LCG(rc, finish, rename(iwxstr_ptr(backup_dir_tmp), iwxstr_ptr(backup_dir)));
  backup = true;

  iwlog_info("Backup has been created in %s", iwxstr_ptr(backup_dir));

  LCG(rc, finish, stat(g_env.program, &selfstat));
  LCG(rc, finish, chmod(iwxstr_ptr(upd_path), selfstat.st_mode));
  LCG(rc, finish, rename(iwxstr_ptr(upd_path), g_env.program));

  iwlog_info("Update has been applied: %s", iwxstr_ptr(backup_dir));

finish:
  if (!backup) {
    iwp_removedir(iwxstr_ptr(backup_dir_tmp));
  }

  iwxstr_destroy(upd_path);
  iwxstr_destroy(backup_dir);
  iwxstr_destroy(backup_dir_tmp);
  iwxstr_destroy(backup_bin_path);
  iwxstr_destroy(backup_db_path);

  return rc;
}

#else

iwrc gr_upd_process_license(const char *resp) {
  return 0;
}

iwrc gr_upd_apply() {
  return 0;
}

#endif // !LICENSE
