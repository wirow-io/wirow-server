#include "gr.h"
#include "iwnet_extra.h"
#include "utils/netstring.h"

#include <iowow/iwxstr.h>
#include <iowow/iwconv.h>

#include <libdeflate.h>
#include <stdlib.h>
#include <errno.h>

int iwn_http_response_gz(
  struct iwn_http_req *req,
  const char          *content_type,
  const char          *data,
  ssize_t              data_len,
  bool                 gzip
  ) {
  if (!gzip || data_len <= 255) {
    return iwn_http_response_write(req, 200, content_type, data, data_len) ? 1 : -1;
  }

  int ret = 500;
  iwrc rc = 0;
  size_t out_len = data_len + 32; // GZIP_MIN_OVERHEAD = 18
  char *out = malloc(out_len);
  struct libdeflate_compressor *c = libdeflate_alloc_compressor(3);
  RCA(c, finish);
  RCA(out, finish);

  size_t len = libdeflate_gzip_compress(c, data, data_len, out, out_len);
  if (len == 0) {
    rc = IW_ERROR_FAIL;
    goto finish;
  }

  iwn_http_response_header_add(req, "content-encoding", "gzip", IW_LLEN("gzip"));
  ret = iwn_http_response_write(req, 200, content_type, out, len) ? 1 : -1;

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  libdeflate_free_compressor(c);
  free(out);
  return ret;
}

int iwn_http_response_jbn(
  struct iwn_http_req *req,
  JBL_NODE             jbn,
  int                  code,
  bool                 gzip
  ) {
  iwrc rc = 0;
  int ret = 500;
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);
  RCC(rc, finish, jbn_as_json(jbn, jbl_xstr_json_printer, xstr, 0));
  ret = iwn_http_response_gz(req, "application/json", iwxstr_ptr(xstr), iwxstr_size(xstr), gzip);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  iwxstr_destroy(xstr);
  return ret;
}

int iwn_http_response_jbl(
  struct iwn_http_req *req,
  JBL                  jbl,
  int                  code,
  bool                 gzip
  ) {
  iwrc rc = 0;
  int ret = 500;
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);
  RCC(rc, finish, jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0));
  ret = iwn_http_response_gz(req, "application/json", iwxstr_ptr(xstr), iwxstr_size(xstr), gzip);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  iwxstr_destroy(xstr);
  return ret;
}

bool iwn_ws_server_write_jbl(struct iwn_ws_sess *ws, JBL json, bool as_netstring) {
  iwrc rc = 0;
  bool ret = false;
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);
  RCC(rc, finish, jbl_as_json(json, jbl_xstr_json_printer, xstr, 0));

  if (as_netstring) {
    char nbuf[NUMBUSZ];
    iwitoa(iwxstr_size(xstr), nbuf, sizeof(nbuf));
    RCC(rc, finish, iwxstr_unshift(xstr, ":", 1));
    RCC(rc, finish, iwxstr_unshift(xstr, nbuf, strlen(nbuf)));
    RCC(rc, finish, iwxstr_cat(xstr, ",", 1));
  }

  ret = iwn_ws_server_write(ws, iwxstr_ptr(xstr), iwxstr_size(xstr));

finish:
  iwxstr_destroy(xstr);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return ret;
}

bool iwn_ws_server_write_jbn(struct iwn_ws_sess *ws, JBL_NODE json, bool as_netstring) {
  iwrc rc = 0;
  bool ret = false;
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);
  RCC(rc, finish, jbn_as_json(json, jbl_xstr_json_printer, xstr, 0));

  if (as_netstring) {
    char nbuf[NUMBUSZ];
    iwitoa(iwxstr_size(xstr), nbuf, sizeof(nbuf));
    RCC(rc, finish, iwxstr_unshift(xstr, ":", 1));
    RCC(rc, finish, iwxstr_unshift(xstr, nbuf, strlen(nbuf)));
    RCC(rc, finish, iwxstr_cat(xstr, ",", 1));
  }

  ret = iwn_ws_server_write(ws, iwxstr_ptr(xstr), iwxstr_size(xstr));

finish:
  iwxstr_destroy(xstr);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  return ret;
}

void grh_req_data_add(struct iwn_http_req *req, struct grh_user_data *h) {
  pthread_mutex_lock(&req->user_mtx);
  if (req->user_data) {
    h->next = req->user_data;
  }
  req->user_data = h;
  pthread_mutex_unlock(&req->user_mtx);
}

void grh_req_data_set(struct iwn_http_req *req, struct grh_user_data *h) {
  pthread_mutex_lock(&req->user_mtx);
  struct grh_user_data *hh = req->user_data, *pp = 0, *nn = 0;
  for ( ; hh; pp = hh, hh = hh->next) {
    if (h->type == hh->type) {
      nn = hh->next;
      if (hh->close) {
        hh->close(hh);
      }
      if (hh->dispose) {
        hh->dispose(hh);
      } else {
        free(hh);
      }
      if (pp) {
        pp->next = h;
      } else {
        req->user_data = h;
      }
      h->next = nn;
      pthread_mutex_unlock(&req->user_mtx);
      return;
    }
  }
  if (req->user_data) {
    h->next = req->user_data;
  }
  req->user_data = h;
  pthread_mutex_unlock(&req->user_mtx);
}

void grh_req_data_dispose_all(struct iwn_http_req *req) {
  pthread_mutex_lock(&req->user_mtx);
  for (struct grh_user_data *h = req->user_data; h; h = h->next) {
    if (h->close) {
      h->close(h);
    }
  }
  struct grh_user_data *h = req->user_data;
  while (h) {
    struct grh_user_data *n = h->next;
    if (h->dispose) {
      h->dispose(h);
    } else {
      free(h);
    }
    h = n;
  }
  pthread_mutex_unlock(&req->user_mtx);
}

void grh_req_data_dispose(struct iwn_http_req *req, int type) {
  pthread_mutex_lock(&req->user_mtx);
  struct grh_user_data *h = req->user_data, *p = 0;
  while (h) {
    struct grh_user_data *n = h->next;
    if (h->type == type) {
      if (h->close) {
        h->close(h);
      }
      if (h->dispose) {
        h->dispose(h);
      } else {
        free(h);
      }
      if (p) {
        p->next = n;
      } else {
        req->user_data = n;
      }
    } else {
      p = h;
    }
    h = n;
  }
  pthread_mutex_unlock(&req->user_mtx);
}

struct grh_user_data* grh_req_data_find(struct iwn_http_req *req, int type) {
  pthread_mutex_lock(&req->user_mtx);
  struct grh_user_data *h = req->user_data;
  for ( ; h; h = h->next) {
    if (h->type == type) {
      pthread_mutex_unlock(&req->user_mtx);
      return h;
    }
  }
  pthread_mutex_unlock(&req->user_mtx);
  return 0;
}
