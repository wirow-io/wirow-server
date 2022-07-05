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

#include "grh_uploads.h"
#include "grh_auth.h"

#include <iowow/iwuuid.h>
#include <iwnet/iwn_mimetypes.h>
#include <iwnet/iwn_wf_files.h>
#include <iwnet/iwn_codec.h>

#include <string.h>
#include <regex.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static bool _file_name_is_valid(const struct iwn_val *filename) {
  for (size_t i = 0; i < filename->len; ++i) {
    switch (filename->buf[i]) {
      case '.':
        if (i > 0 && filename->buf[i - 1] == '.') {
          return false;
        }
        break;
      case '/':
      case '\\':
        return false;
    }
  }
  return true;
}

static char* _file_path_create(const char *uuid, bool rdonly) {
  iwrc rc = 0;
  char *ret = 0;
  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return 0;
  }
  if (iwxstr_printf(xstr, "%s/%.2s/%.2s", g_env.uploads.dir, uuid, uuid + 2)) {
    iwxstr_destroy(xstr);
    return 0;
  }
  if (!rdonly) {
    if ((rc = iwp_mkdirs(iwxstr_ptr(xstr)))) {
      iwlog_ecode_error(rc, "Failed to create directory: %s", iwxstr_ptr(xstr));
      iwxstr_destroy(xstr);
      return 0;
    }
  }
  if (iwxstr_printf(xstr, "/%s", uuid)) {
    iwxstr_destroy(xstr);
    return 0;
  }
  return iwxstr_destroy_keep_ptr(xstr);
}

static char* _file_link_create(struct iwn_wf_req *req, const char *uuid, const struct iwn_val *fname) {
  const char *sp = strrchr(req->path, '/');
  if (!sp) {
    return 0;
  }
  intptr_t baselen = sp - req->path;
  char *ret = 0;
  IWXSTR *xstr = iwxstr_new();
  if (!xstr) {
    return 0;
  }
  //fname_encoded = iwn_url_encode_new(fname->buf, fname->len);
  //if (!fname_encoded) {
  //  iwxstr_destroy(xstr);
  //  return 0;
  //}
  if (iwxstr_printf(xstr, "%.*s/%s/%.*s", (int) baselen, req->path, uuid, (int) fname->len, fname->buf)) {
    iwxstr_destroy(xstr);
    return 0;
  }
  return iwxstr_destroy_keep_ptr(xstr);
}

static iwrc _file_persist(struct iwn_pair *file, const char *path) {
  size_t aunit = iwp_alloc_unit();
  int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    return iwrc_set_errno(IW_ERROR_IO_ERRNO, errno);
  }
  if (ftruncate(fd, file->val_len) == -1) {
    return iwrc_set_errno(IW_ERROR_IO_ERRNO, errno);
  }
  size_t mlen = IW_ROUNDUP(file->val_len, aunit);
  void *mm = mmap(0, mlen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mm == MAP_FAILED) {
    return iwrc_set_errno(IW_ERROR_IO_ERRNO, errno);
  }
  memcpy(mm, file->val, file->val_len);
  munmap(mm, mlen);
  fsync(fd);
  close(fd);
  return 0;
}

static iwrc _file_meta_persist(
  int64_t               uid,
  const char           *uuid,
  const struct iwn_val *fname,
  const char           *ctype
  ) {
  iwrc rc = 0;
  int64_t id;
  uint64_t ts;
  JBL_NODE n, n2;
  IWPOOL *pool;

  RCB(finish, pool = iwpool_create_empty());

  RCC(rc, finish, iwp_current_time_ms(&ts, false));
  RCC(rc, finish, jbn_from_json_printf(
        &n, pool,
        "{\"uuid\": \"\", \"filename\":\"\", "
        "\"owner\": %" PRIu64 ", \"ctime\": %" PRIu64 ", "
        "\"ctype\":\"\"}", uid, ts));
  if (!jbn_at(n, "/uuid", &n2) && n2->type == JBV_STR) {
    n2->vptr = uuid;
    n2->vsize = IW_UUID_STR_LEN;
  }
  if (!jbn_at(n, "/filename", &n2) && n2->type == JBV_STR) {
    n2->vptr = fname->buf,
    n2->vsize = (int) fname->len;
  }
  if (!jbn_at(n, "/ctype", &n2) && n2->type == JBV_STR) {
    if (ctype) {
      n2->vptr = ctype;
      n2->vsize = (int) strlen(ctype);
    } else {
      jbn_remove_item(n, n2);
    }
  }

  rc = ejdb_put_new_jbn(g_env.db, "files", n, &id);

finish:
  iwpool_destroy(pool);
  return rc;
}

static int _upload(struct iwn_wf_req *req, void *d) {
  iwrc rc = 0;
  int ret = 500;
  char uuid[IW_UUID_STR_LEN + 1];

  int64_t uid = grh_auth_get_userid(req);
  if (uid == 0 || !grh_auth_has_any_perms(req, "user")) {
    return 403;
  }

  struct iwn_pair *file = iwn_pair_find(&req->form_params, "file", IW_LLEN("file"));
  if (!file->val_len) {
    return 400;
  }

  struct iwn_val fname = iwn_pair_find_val(file->extra, "filename", IW_LLEN("filename"));
  fname.len = iwn_url_decode_inplace2(fname.buf, fname.buf + fname.len);
  if (!_file_name_is_valid(&fname)) {
    return 400;
  }

  iwu_uuid4_fill(uuid);
  uuid[IW_UUID_STR_LEN] = '\0';

  char *path = _file_path_create(uuid, false);
  if (!path) {
    return 500;
  }
  char *link = _file_link_create(req, uuid, &fname);
  if (!link) {
    free(path);
    return 500;
  }

  RCC(rc, finish, _file_persist(file, path));

  const char *ctype = iwn_mimetype_find_by_path2(fname.buf, fname.len);
  if (!ctype) {
    ctype = "application/octet-stream";
  }
  RCC(rc, finish, _file_meta_persist(uid, uuid, &fname, ctype));

  ret = iwn_http_response_write(req->http, 200, "text/plain", link, -1) ? 1 : -1;

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  free(link);
  free(path);

  return ret;
}

static int _file_del(struct iwn_wf_req *req, const char *uuid) {
  iwrc rc;
  JBL_NODE n;
  EJDB_DOC doc;
  JQL q = 0;
  EJDB_LIST list = 0;
  char *path = 0;
  int ret = 500;

  RCC(rc, finish, jql_create(&q, "files", "/[uuid = :?] | /{owner}"));
  RCC(rc, finish, jql_set_str(q, 0, 0, uuid));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));

  doc = list->first;
  if (!doc) {
    ret = 404;
    goto finish;
  }

  bool has_rights = grh_auth_has_any_perms(req, "admin");
  if (!has_rights) {
    uint64_t uid = grh_auth_get_userid(req);
    if (!jbn_at(doc->node, "/owner", &n) && n->type == JBV_I64) {
      has_rights = uid == n->vi64;
    }
  }

  if (!has_rights) {
    ret = 403;
    goto finish;
  }

  path = _file_path_create(uuid, true);

  ejdb_del(g_env.db, "files", doc->id);
  unlink(path);

  ret = 201;

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  free(path);
  return ret;
}

static int _file_get(struct iwn_wf_req *req, const char *uuid) {
  iwrc rc = 0;
  JBL_NODE n;
  EJDB_DOC doc;
  JQL q = 0;
  int ret = 500;
  char *path = 0;
  EJDB_LIST list = 0;
  const char *fname = 0, *ctype = 0;
  char *fname_encoded = 0;

  RCC(rc, finish, jql_create(&q, "files", "/[uuid = :?] | /{owner, filename, ctype}"));
  RCC(rc, finish, jql_set_str(q, 0, 0, uuid));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));

  doc = list->first;
  if (!doc) {
    ret = 404;
    goto finish;
  }

  if (!jbn_at(doc->node, "/filename", &n) && n->type == JBV_STR) {
    fname = n->vptr;
  } else {
    ret = 404;
    goto finish;
  }

  size_t len = iwn_url_encoded_len(fname, n->vsize) + 1;
  RCB(finish, fname_encoded = malloc(len));
  iwn_url_encode(fname, n->vsize, fname_encoded, len);

  if (!jbn_at(doc->node, "/ctype", &n) && n->type == JBV_STR) {
    ctype = n->vptr;
  } else {
    ctype = iwn_mimetype_find_by_path(fname);
  }
  if (ctype == 0) {
    ret = 404;
    goto finish;
  }

  RCC(rc, finish, iwn_http_response_header_printf(
        req->http,
        "content-disposition",
        "inline; filename=\"%s\"; filename*=UTF-8''%s", fname_encoded, fname_encoded));

  path = _file_path_create(uuid, true);
  if (!path) {
    goto finish;
  }

  ret = iwn_wf_file_serve(req, ctype, path);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  free(path);
  free(fname_encoded);

  return ret;
}

static int _file(struct iwn_wf_req *req, void *d) {
  struct iwn_wf_route_submatch *sm = iwn_wf_request_submatch_first(req);
  if (!sm || sm->ep - sm->sp != IW_UUID_STR_LEN) {
    return 500;
  }
  char uuid[IW_UUID_STR_LEN + 1];
  memcpy(uuid, sm->sp, IW_UUID_STR_LEN);
  uuid[IW_UUID_STR_LEN] = '\0';

  if (req->flags & IWN_WF_DELETE) {
    return _file_del(req, uuid);
  } else if (req->flags & IWN_WF_GET) {
    return _file_get(req, uuid);
  }
  return 500;
}

iwrc grh_route_uploads(struct iwn_wf_route *parent) {
  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/files",
    .flags = IWN_WF_POST | IWN_WF_DELETE | IWN_WF_GET
  }, &parent));

  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/upload",
    .handler = _upload,
    .flags = IWN_WF_POST,
  }, 0));

  RCR(iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "^/([a-z0-9]{8}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{12})(/.*)?",
    .handler = _file,
    .flags = IWN_WF_GET | IWN_WF_DELETE
  }, 0));

  return 0;
}
