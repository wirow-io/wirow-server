#include "grh_session.h"

#include <ejdb2/ejdb2.h>
#include <ejdb2/iowow/iwpool.h>
#include <ejdb2/iowow/iwp.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

extern struct gr_env g_env;

static char* _get(struct iwn_wf_session_store *sst, const char *sid, const char *key) {
  if (!sst || !sid || !key) {
    iwlog_ecode_error3(IW_ERROR_INVALID_ARGS);
    return 0;
  }

  iwrc rc = 0;
  char *ret = 0;
  JQL q = 0;
  EJDB_LIST list = 0;

  RCC(rc, finish, jql_create(&q, "sessions", "/[__id__ = :sid] and /[* = :key] | /:key"));
  RCC(rc, finish, jql_set_str(q, "sid", 0, sid));
  RCC(rc, finish, jql_set_str(q, "key", 0, key));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));

  if (list->first) {
    JBL_NODE n = list->first->node->child;
    assert(n);
    if (n->type == JBV_STR) {
      ret = strdup(n->vptr);
    }
  }

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  return ret;
}

static iwrc _put(struct iwn_wf_session_store *sst, const char *sid, const char *key, const char *val) {
  if (!sst || !sid || !key) {
    return IW_ERROR_INVALID_ARGS;
  }
  JBL jbl;
  uint64_t ts;
  iwrc rc = 0;
  JQL q = 0;

  RCC(rc, finish, iwp_current_time_ms(&ts, false));
  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "__id__", sid));
  RCC(rc, finish, jbl_set_int64(jbl, "__ts__", ts));
  if (val) {
    RCC(rc, finish, jbl_set_string(jbl, key, val));
  } else {
    RCC(rc, finish, jbl_set_null(jbl, key));
  }
  RCC(rc, finish, jql_create(&q, "sessions", "/[__id__ = :sid] | upsert :doc"));
  RCC(rc, finish, jql_set_str(q, "sid", 0, sid));
  RCC(rc, finish, jql_set_json_jbl(q, "doc", 0, jbl));
  rc = ejdb_update(g_env.db, q);

finish:
  jql_destroy(&q);
  jbl_destroy(&jbl);
  return rc;
}

static void _del(struct iwn_wf_session_store *sst, const char *sid, const char *key) {
  iwrc rc = _put(sst, sid, key, 0);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static void _clear(struct iwn_wf_session_store *sst, const char *sid) {
  if (!sst || !sid) {
    return;
  }
  JQL q;
  iwrc rc;
  RCC(rc, finish, jql_create(&q, "sessions", "/[__id__ = :sid] | del"));
  RCC(rc, finish, jql_set_str(q, "sid", 0, sid));
  rc = ejdb_update(g_env.db, q);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jql_destroy(&q);
}

static void _dispose(struct iwn_wf_session_store *sst) {
  struct ctx *ctx = sst->user_data;
  free(ctx);
}

iwrc grh_session_store_create(struct iwn_wf_session_store *ss) {
  iwrc rc = 0;
  ss->get = _get;
  ss->put = _put;
  ss->del = _del;
  ss->clear = _clear;
  ss->dispose = _dispose;
  return rc;
}
