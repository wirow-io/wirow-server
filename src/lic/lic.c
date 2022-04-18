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
#include "gr_crypt.h"
#include "lic.h"
#include "lic_env.h"
#include "lic_vars.h"
#include "upd/upd.h"
#include "utils/urandom.h"
#include "utils/xcurl.h"

#include <iowow/iwp.h>
#include <iowow/iwxstr.h>
#include <ejdb2/jbl.h>
#include <iwnet/bearssl_hash.h>
#include <iwnet/bearssl_block.h>
#include <iwnet/iwn_base64.h>
#include <iwnet/iwn_scheduler.h>
#include <iwnet/iwnet.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdlib.h>

#ifdef LICENSE_ID

extern struct gr_env g_env;

#define LICENSE_BLOB_MAX_SIZE 1024
#define TELEMETRY_BUF_SIZE    128

static struct {
  volatile const char *error; /**< License error state */
  atomic_int error_count;
} license;


static iwrc _license_check_start(void);

static void _license_state_report(iwrc rc, long code, IWXSTR *resp) {
  const char *raw = resp ? iwxstr_ptr(resp) : 0;
  const char *status = 0;
  if (raw == 0) {
    status = LICENSE_SUPPORT_REQUIRED;
  } else if (!strcmp(LICENSE_CORRUPTED_OR_CLASHED, raw)) {
    status = LICENSE_CORRUPTED_OR_CLASHED;
  } else if (!strcmp(LICENSE_CORRUPTED_OR_CLASHED, raw)) {
    status = LICENSE_INACTIVE;
  } else if (!strcmp(LICENSE_UNKNOWN, raw)) {
    status = LICENSE_UNKNOWN;
  } else {
    // Here is all other cases including LICENSE_VERIFICATION_FAILED
    status = LICENSE_SUPPORT_REQUIRED;
  }
  license.error = status;
  ++license.error_count;
}

static iwrc _fetch_hash_and_blob(
  uint8_t out_hash[static br_sha256_SIZE],
  uint8_t out_license_blob[static LICENSE_BLOB_MAX_SIZE]
  ) {
  ssize_t rb;
  char path[PATH_MAX];
  IWP_FILE_STAT fstat;
  br_sha256_context ctx;

  iwrc rc = RCR(iwp_exec_path(path));
  RCR(iwp_fstat(path, &fstat));

  if (fstat.size < LICENSE_BLOB_MAX_SIZE) {
    return IW_ERROR_INVALID_VALUE;
  }
  off_t end = fstat.size - LICENSE_BLOB_MAX_SIZE;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return iwrc_set_errno(IW_ERROR_ERRNO, errno);
  }
  br_sha256_init(&ctx);
  while (end > 0) {
    unsigned char buf[4096];
    rb = read(fd, buf, MIN(end, sizeof(buf)));
    if (rb == -1) {
      if (errno == EINTR) {
        continue;
      }
      rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
      goto finish;
    }
    br_sha256_update(&ctx, buf, rb);
    end -= rb;
  }
  br_sha256_out(&ctx, out_hash);

again:
  rb = read(fd, out_license_blob, LICENSE_BLOB_MAX_SIZE);
  if (rb == -1) {
    if (errno == EINTR) {
      goto again;
    }
    rc = iwrc_set_errno(IW_ERROR_ERRNO, errno);
  } else if (rb < LICENSE_BLOB_MAX_SIZE) {
    rc = IW_ERROR_INVALID_VALUE;
  }

finish:
  if (fd > -1) {
    close(fd);
  }
  return rc;
}

static iwrc _telemetry_fill(uint8_t out_tele[static 128]) {
  memset(out_tele, 0, 128);
  // todo: compute basic telemetry
  return 0;
}

static iwrc _ckey_fill(uint8_t out_ck[static 32]) {
  JBL jbl = 0;
  unsigned char *key = 0;
  const char *raw;
  EJDB db = g_env.db;

  iwrc rc = ejdb_get(db, "lic", 1, &jbl);
  if (!rc) {
    const char *ick = 0;
    jbl_object_get_str(jbl, "ick", &ick);
    if (ick && (strcmp(ick, LICENSE_CK) != 0)) {
      // Initial hardcoded client key changed.
      // It may caused by clean rebuild?
      // So force resetting `lic` state
      rc = IW_ERROR_NOT_EXISTS;
    }
  }
  if ((rc == IW_ERROR_NOT_EXISTS) || (rc == IWKV_ERROR_NOTFOUND)) {
    RCC(rc, finish, jbl_create_empty_object(&jbl));
    RCC(rc, finish, jbl_set_string(jbl, "ck", LICENSE_CK));
    RCC(rc, finish, jbl_set_string(jbl, "ick", LICENSE_CK));
    RCC(rc, finish, ejdb_put(db, "lic", jbl, 1));
    raw = LICENSE_CK;
  } else {
    RCC(rc, finish, jbl_object_get_str(jbl, "ck", &raw));
  }

  size_t klen = strlen(raw);
  RCB(finish, key = malloc(klen));

  if (iwn_base64_decode(key, klen, raw, klen, 0, &klen, 0, base64_VARIANT_ORIGINAL) == -1 || klen != 32) {
    iwlog_error2("Invalid ck key value");
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }
  memcpy(out_ck, key, 32);

finish:
  jbl_destroy(&jbl);
  free(key);
  return rc;
}

static iwrc _decrypt_buf(void *buf, void **decrypted, size_t *len) {
  char tag[16], ckey[32];
  if (*len <= 12 + 16) { // nonce + tag
    return GR_ERROR_COMMAND_INVALID_INPUT;
  }

  size_t data_len = *len - 12 - 16;
  char *nonce = buf;
  const char *mtag = nonce + *len - 16; // tag arrived in message
  char *data = nonce + 12;

  RCR(_ckey_fill((void*) ckey));

  br_poly1305_run poly_run = br_poly1305_ctmulq_get();
  if (!poly_run) {
    poly_run = br_poly1305_ctmul_run;
  }
  br_chacha20_run chacha_run = br_chacha20_sse2_get();
  if (!chacha_run) {
    chacha_run = br_chacha20_ct_run;
  }

  poly_run(ckey, nonce, data, data_len, 0, 0, tag, chacha_run, 0);

  if (memcmp(tag, mtag, 16) != 0) {
    return GR_ERROR_DECRYPT_FAILED;
  }

  *decrypted = data;
  *len = data_len;
  return 0;
}

static iwrc _decrypt_base64(IWXSTR *xstr) {
  iwrc rc = 0;
  void *decrypted;
  unsigned char *res;
  size_t len = iwxstr_size(xstr);

  RCB(finish, res = malloc(len));
  if (iwn_base64_decode(res, len, iwxstr_ptr(xstr), len, 0, &len, 0, base64_VARIANT_ORIGINAL) == -1) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }
  RCC(rc, finish, _decrypt_buf(res, &decrypted, &len));
  iwxstr_clear(xstr);
  rc = iwxstr_cat(xstr, decrypted, len);

finish:
  free(res);
  return rc;
}

static iwrc _licence_check_send(CURL *curl, const char *payload, IWXSTR *out) {
  iwrc rc = 0;
  CURLcode cc = 0;

  curl_easy_reset(curl);
  // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

  struct curl_blob cainfo_blob = {
    .data  = (void*) iwn_cacerts,
    .len   = iwn_cacerts_len,
    .flags = CURL_BLOB_NOCOPY,
  };

  struct curl_slist *headers = curl_slist_append(curl_slist_append(0, "Expect:"), "X-License-Id: " LICENSE_ID);
  RCA(headers, finish);

  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_URL, LICENSE_SERVER "/license/check"));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &cainfo_blob));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, xcurl_body_write_xstr));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_WRITEDATA, out));

  struct xcurl_cursor dcur = {
    .rp  = payload,
    .end = payload + strlen(payload)
  };
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READFUNCTION, xcurl_read_cursor));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READDATA, &dcur));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_INFILESIZE, dcur.end - dcur.rp));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, LICENSE_SSL_VERIFY));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, LICENSE_SSL_VERIFY));

  XCC(cc, finish, curl_easy_perform(curl));

finish:
  curl_slist_free_all(headers);
  return rc;
}

static iwrc _license_check_confirm(CURL *curl, const char *resp, long *out_next_check_timeout) {
  JBL jbl = 0;
  JBL_NODE n, n2;
  const char *ch, *ckn;

  struct curl_slist *headers = 0;
  CURLcode cc = 0;

  IWPOOL *pool = iwpool_create_empty();
  if (!pool) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }

  iwrc rc = jbn_from_json(resp, &n, pool);
  RCGO(rc, finish);
  RCC(rc, finish, jbn_at(n, "/ch", &n2)); // confirmation hook
  if (n2->type != JBV_STR) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }
  ch = n2->vptr;
  RCC(rc, finish, jbn_at(n, "/ckn", &n2)); // new client key
  if (n2->type != JBV_STR) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }
  ckn = n2->vptr;
  RCC(rc, finish, jbn_at(n, "/nc", &n2));
  if (n2->type != JBV_I64) {
    rc = GR_ERROR_INVALID_DATA_RECEIVED;
    goto finish;
  }

  *out_next_check_timeout = (long) n2->vi64;

  // Great, now we have to confirm license processing
  curl_easy_reset(curl);

  struct curl_blob cainfo_blob = {
    .data  = (void*) iwn_cacerts,
    .len   = iwn_cacerts_len,
    .flags = CURL_BLOB_NOCOPY,
  };

  RCB(finish, headers = curl_slist_append(curl_slist_append(0, "Expect:"), "X-License-Id: " LICENSE_ID));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_URL, LICENSE_SERVER "/license/confirm"));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &cainfo_blob));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L));

  struct xcurl_cursor dcur = {
    .rp  = ch,
    .end = ch + strlen(ch)
  };
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READFUNCTION, xcurl_read_cursor));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_READDATA, &dcur));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_INFILESIZE, dcur.end - dcur.rp));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, LICENSE_SSL_VERIFY));
  XCC(cc, finish, curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, LICENSE_SSL_VERIFY));

  XCC(cc, finish, curl_easy_perform(curl));

  RCC(rc, finish, jbl_from_json(&jbl, "{}"));
  RCC(rc, finish, jbl_set_string(jbl, "ck", ckn));
  RCC(rc, finish, ejdb_merge_or_put_jbl(g_env.db, "lic", jbl, 1));

finish:
  jbl_destroy(&jbl);
  iwpool_destroy(pool);
  curl_slist_free_all(headers);
  return rc;
}

static void _license_check_do() {
  iwrc rc = _license_check_start();
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static void _license_next_check_schedule(long nct_sec) {
  if (nct_sec < 1) {
    nct_sec = 300;
  }
  iwlog_debug("Next license check in %ld seconds", nct_sec);
  iwrc rc = iwn_schedule(&(struct iwn_scheduler_spec) {
    .poller = g_env.poller,
    .task_fn = _license_check_do,
    .timeout_ms = nct_sec * 1000
  });
  if (rc) {
    iwlog_ecode_error(rc, "Failed to do license checking, exiting...");
    exit(EXIT_FAILURE);
  }
}

static iwrc _license_check_start(void) {
  CURL *curl = 0;
  uint8_t ck[32], *tag;
  size_t data_len;
  uint8_t sink[12 + br_sha256_SIZE + TELEMETRY_BUF_SIZE + LICENSE_BLOB_MAX_SIZE + 16] = { 0 };
  uint8_t *nonce = sink;
  uint8_t *data = nonce + 12;
  uint8_t *blob = data + br_sha256_SIZE + TELEMETRY_BUF_SIZE;

  IWXSTR *xstr = 0, *cxstr = 0;
  char *payload = 0;
  long code = 0, next_check_timeout = 0;

  uint8_t *wp = data;
  iwrc rc = _fetch_hash_and_blob(wp, blob);
  RCGO(rc, finish);
  wp += br_sha256_SIZE;

  RCC(rc, finish, _telemetry_fill(wp));
  wp += TELEMETRY_BUF_SIZE;

  int16_t blen = *((int16_t*) blob);
  data_len = wp - data + sizeof(int16_t) + blen;
  tag = data + data_len;

  // Now encypt block
  br_poly1305_run poly_run = br_poly1305_ctmulq_get();
  if (!poly_run) {
    poly_run = br_poly1305_ctmul_run;
  }
  br_chacha20_run chacha_run = br_chacha20_sse2_get();
  if (!chacha_run) {
    chacha_run = br_chacha20_ct_run;
  }

  RCC(rc, finish, _ckey_fill(ck));
  RCC(rc, finish, urandom(nonce, 12)); // Fill nonce

  poly_run(ck, nonce, data, data_len, 0, 0, tag, chacha_run, 1);

  data_len = tag + 16 - sink;
  data_len = iwn_base64_encoded_len(data_len, base64_VARIANT_ORIGINAL);
  RCB(finish, payload = malloc(data_len));
  if (!iwn_base64_encode(payload, data_len, &data_len, sink, (int) (tag + 16 - sink), base64_VARIANT_ORIGINAL)) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }

  curl = curl_easy_init();
  if (!curl) {
    rc = GR_ERROR_CURL_API_FAIL;
    goto finish;
  }

  RCB(finish, xstr = iwxstr_new());
  cxstr = xstr;
  RCC(rc, finish, _licence_check_send(curl, payload, xstr));

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  if (code != 200) {
    goto finish;
  }
  cxstr = 0; // Avoid to pass response junk into _license_state_report

  RCC(rc, finish, _decrypt_base64(xstr));
  RCC(rc, finish, _license_check_confirm(curl, iwxstr_ptr(xstr), &next_check_timeout));
  RCC(rc, finish, gr_upd_process_license(iwxstr_ptr(xstr)));

  // All is
  license.error = 0;
  license.error_count = 0;

finish:
  if (rc || (code != 200)) {
    _license_state_report(rc, code, cxstr);
  }
  free(payload);
  curl_easy_cleanup(curl);
  iwxstr_destroy(xstr);

  // Anyway we will do next check
  _license_next_check_schedule(next_check_timeout);

  return rc;
}

void gr_lic_error(const char **out) {
  int cnt = license.error_count;
  if (cnt > 2) {
    *out = (void*) license.error;
  } else {
    *out = 0;
  }
}

void gr_lic_warning(const char **out) {
  *out = (void*) license.error;
}

const char* gr_lic_summary(void) {
  return "\n\n\tLicensed to " LICENSE_OWNER "\n\tLicense terms: " LICENSE_TERMS "\n";
}

static bool _initialized = false;

iwrc gr_lic_init(void) {
  if (!__sync_bool_compare_and_swap(&_initialized, false, true)) {
    return 0;  // initialized already
  }
  iwlog_info2(gr_lic_summary());
  _license_check_do();
  return 0;
}

#else

void gr_lic_error(const char **out) {
  *out = 0;
}

void gr_lic_warning(const char **out) {
  *out = 0;
}

const char* gr_lic_summary(void) {
  return 0;
}

iwrc gr_lic_init(void) {
  return 0;
}

#endif // !LICENSE
