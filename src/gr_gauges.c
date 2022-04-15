#include "gr_gauges.h"

#include "gr.h"
#include "grh_ws.h"
#include "grh_auth.h"

#include <stdlib.h>
#include <string.h>

extern struct gr_env g_env;

struct gcheck {
  uint64_t    ts;
  uint32_t    gauge;
  int64_t     level;
  const char *message;
};

struct gctx {
  int64_t ts_from;
  IWXSTR *xstr;
  int32_t first;
};

struct gset {
  uint32_t gauge;
  int64_t  level;
};

static bool _gauge_notify_visitor(struct ws_session *wss, void *op) {
  if (!grh_auth_has_any_perms(wss->ws->req, "admin")) {
    return true;
  }
  struct gcheck *gc = op;
  grh_ws_send_by_wsid(wss->wsid, gc->message, -1);
  return true;
}

static void _gauge_notify(void *op) {
  // Notify admins about gauge change
  iwrc rc = 0;
  struct gcheck *gc = op;
  JBL jbl = 0, jbl2 = 0;
  IWXSTR *xstr = iwxstr_new();
  RCA(xstr, finish);

  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_string(jbl, "event", "GAUGE"));

  RCC(rc, finish, jbl_create_empty_array(&jbl2));
  RCC(rc, finish, jbl_set_int64(jbl2, 0, gc->ts));
  RCC(rc, finish, jbl_set_int64(jbl2, 0, gc->gauge));
  RCC(rc, finish, jbl_set_int64(jbl2, 0, gc->level));
  RCC(rc, finish, jbl_set_nested(jbl, "data", jbl2));

  RCC(rc, finish, jbl_as_json(jbl, jbl_xstr_json_printer, xstr, 0));
  gc->message = iwxstr_ptr(xstr);

  rc = grh_ws_visit_clients(_gauge_notify_visitor, op);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  jbl_destroy(&jbl);
  jbl_destroy(&jbl2);
  iwxstr_destroy(xstr);
  free(gc);
}

static iwrc _gauge_set_with_ts(uint64_t ts, uint32_t gauge, int64_t level, bool notify) {
  iwrc rc = 0;
  JQL q = 0;
  EJDB_LIST list = 0;
  JBL jbl = 0;
  int64_t llv;

  ts /= 1000; // ms => sec

  RCC(rc, finish, jql_create(&q, "gauges", "/[g = :?]"));
  RCC(rc, finish, jql_set_i64(q, 0, 0, gauge));
  RCC(rc, finish, ejdb_list4(g_env.db, q, 1, 0, &list));

  if (!list->first || jbl_object_get_i64(list->first->raw, "l", &llv) || llv != level) {
    RCC(rc, finish, jbl_create_empty_object(&jbl));
    RCC(rc, finish, jbl_set_int64(jbl, "t", ts));
    RCC(rc, finish, jbl_set_int64(jbl, "g", gauge));
    RCC(rc, finish, jbl_set_int64(jbl, "l", level));
    RCC(rc, finish, ejdb_put_new(g_env.db, "gauges", jbl, &llv));
    if (notify) {
      struct gcheck *gc = malloc(sizeof(*gc));
      if (gc) {
        *gc = (struct gcheck) {
          .ts = ts,
          .gauge = gauge,
          .level = level
        };
        if (iwtp_schedule(g_env.tp, _gauge_notify, gc)) {
          free(gc);
        }
      }
    }
  }

finish:
  jql_destroy(&q);
  ejdb_list_destroy(&list);
  jbl_destroy(&jbl);
  return rc;
}

static iwrc _gauge_set(uint32_t gauge, int64_t level) {
  uint64_t ts;
  RCR(iwp_current_time_ms(&ts, false));
  return _gauge_set_with_ts(ts, gauge, level, true);
}

static void _gauge_set_async_task(void *op) {
  struct gset *gset = op;
  iwrc rc = _gauge_set(gset->gauge, gset->level);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
  free(gset);
}

iwrc gr_gauge_set_async(uint32_t gauge, int64_t level) {
  struct gset *gset = malloc(sizeof(*gset));
  if (!gset) {
    return iwrc_set_errno(IW_ERROR_ALLOC, errno);
  }
  *gset = (struct gset) {
    .gauge = gauge,
    .level = level
  };
  iwrc rc = iwstw_schedule(g_env.stw, _gauge_set_async_task, gset);
  if (rc) {
    free(gset);
  }
  return rc;
}

static iwrc _gauges_reset_all(void) {
  uint64_t ts;
  RCR(iwp_current_time_ms(&ts, false));
  iwrc rc = RCR(iwp_current_time_ms(&ts, false));
  IWRC(_gauge_set_with_ts(ts, GAUGE_ROOMS, 0, 0), rc);
  IWRC(_gauge_set_with_ts(ts, GAUGE_ROOM_USERS, 0, 0), rc);
  IWRC(_gauge_set_with_ts(ts, GAUGE_WORKERS, 0, 0), rc);
  IWRC(_gauge_set_with_ts(ts, GAUGE_STREAMS, 0, 0), rc);
  return rc;
}

static iwrc _get_visitor(EJDB_EXEC *ex, EJDB_DOC doc, int64_t *step) {
  iwrc rc = 0;
  int64_t t, g, l;
  struct gctx *gctx = ex->opaque;

  bool need_comma = true;
  RCC(rc, finish, jbl_object_get_i64(doc->raw, "t", &t));
  RCC(rc, finish, jbl_object_get_i64(doc->raw, "g", &g));
  RCC(rc, finish, jbl_object_get_i64(doc->raw, "l", &l));

  if ((gctx->first & g) == 0) {
    gctx->first |= g;
    uint64_t ts;
    char buf[255];
    RCC(rc, finish, iwp_current_time_ms(&ts, false));
    snprintf(buf, sizeof(buf), "[%" PRId64 ",%" PRId64 ",%" PRId64 "],", (int64_t) (ts / 1000), g, l);
    if (iwxstr_size(gctx->xstr) == 0) {
      need_comma = false;
    }
    RCC(rc, finish, iwxstr_unshift(gctx->xstr, buf, strlen(buf)));
  } else if (ex->cnt == 0) {
    need_comma = false;
  }

  if (need_comma) {
    RCC(rc, finish, iwxstr_cat(gctx->xstr, ",", 1));
  }

  RCC(rc, finish, iwxstr_printf(gctx->xstr, "[%" PRId64 ",%" PRId64 ",%" PRId64 "]", t, g, l));

  if (t <= gctx->ts_from) {
    *step = 0;
    return 0;
  }

finish:
  return rc;
}

static int _gauges_get(int64_t ts_back, struct iwn_wf_req *req) {
  iwrc rc = 0;
  int ret = 500;
  JQL q = 0;
  uint64_t ts_from;

  RCC(rc, finish, iwp_current_time_ms(&ts_from, false));
  ts_from = ts_from / 1000 - ts_back;

  RCC(rc, finish, jql_create(&q, "gauges", "/*"));

  struct gctx gctx = {
    .ts_from = ts_from,
  };
  RCB(finish, gctx.xstr = iwxstr_new());
  EJDB_EXEC ex = {
    .db      = g_env.db,
    .q       = q,
    .visitor = _get_visitor,
    .opaque  = &gctx,
  };

  RCC(rc, finish, ejdb_exec(&ex));
  RCC(rc, finish, iwxstr_unshift(gctx.xstr, "[", 1));
  RCC(rc, finish, iwxstr_cat(gctx.xstr, "]", 1));
  ret = iwn_http_response_gz(req->http, "application/json", iwxstr_ptr(gctx.xstr), iwxstr_size(gctx.xstr), true);

finish:
  iwxstr_destroy(gctx.xstr);
  jql_destroy(&q);
  return ret;
}

static int _12h(struct iwn_wf_req *req, void *d) {
  return _gauges_get(60 * 60 * 12, req);
}

static int _24h(struct iwn_wf_req *req, void *d) {
  return _gauges_get(60 * 60 * 24, req);
}

static int _week(struct iwn_wf_req *req, void *d) {
  return _gauges_get(60 * 60 * 24 * 7, req);
}

static int _month(struct iwn_wf_req *req, void *d) {
  return _gauges_get(60 * 60 * 24 * 7 * 30, req);
}

iwrc grh_route_gauges(const struct iwn_wf_route *parent) {
  iwrc rc = RCR(_gauges_reset_all());

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/12h",
    .handler = _12h,
  }, 0));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/24h",
    .handler = _24h,
  }, 0));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/week",
    .handler = _week,
  }, 0));

  RCC(rc, finish, iwn_wf_route(&(struct iwn_wf_route) {
    .parent = parent,
    .pattern = "/month",
    .handler = _month,
  }, 0));

finish:
  return rc;
}

iwrc _gr_gauges_maintain_type(uint64_t ts, int64_t type, IWPOOL *pool) {
  iwrc rc = 0;
  JQL q;
  JBL_NODE n1, n2, n3;
  EJDB_DOC doc1, doc2;
  int64_t id;

  // EJDB lists rows from recent to older
  // So to make gauges to be accessible when selecting time segment when there were no changes
  // We can just maintain last db entry:
  // If last two entries have same numbers, latest is maintained one, change it's timestamp
  // If last two entries have different numbers, create next with the same to maintain
  // And make sure that history is correct: delete created entry if there is something new

  RCC(rc, finish, jql_create(&q, "gauges", "/[g = :?] | /{t,g,l}"));
  RCC(rc, finish, jql_set_i64(q, 0, 0, type));
  RCC(rc, finish, ejdb_list(g_env.db, q, &doc1, 2, pool));
  if (doc1 && !jbn_at(doc1->node, "/t", &n1) && n1->type == JBV_I64) {
    const bool got_n2 = !jbn_at(doc1->node, "/l", &n2) && n2->type == JBV_I64;
    const bool got_n3 = doc1->next && !jbn_at(doc1->next->node, "/l", &n3) && n3->type == JBV_I64;
    if (doc1->next && got_n2 && got_n3 && n2->vi64 == n3->vi64) {
      n1->vi64 = ts;
      RCC(rc, finish, ejdb_put_jbn(g_env.db, "gauges", doc1->node, doc1->id));
    } else {
      n1->vi64 = ts;
      RCC(rc, finish, ejdb_put_new_jbn(g_env.db, "gauges", doc1->node, &id));

      RCC(rc, finish, ejdb_list(g_env.db, q, &doc2, 2, pool));
      if (doc2 && (doc2->id != id || (doc2->next && doc2->next->id != doc1->id))) {
        RCC(rc, finish, ejdb_del(g_env.db, "gauges", id));
      }
    }
  }

finish:
  jql_destroy(&q);
  return rc;
}

iwrc gr_gauges_maintain(void) {
  iwrc rc = 0;
  IWPOOL *pool = 0;
  uint64_t ts;

  RCR(iwp_current_time_ms(&ts, false));
  ts /= 1000;

  RCB(finish, pool = iwpool_create_empty());
  RCC(rc, finish, _gr_gauges_maintain_type(ts, GAUGE_ROOMS, pool));
  RCC(rc, finish, _gr_gauges_maintain_type(ts, GAUGE_ROOM_USERS, pool));
  RCC(rc, finish, _gr_gauges_maintain_type(ts, GAUGE_WORKERS, pool));
  RCC(rc, finish, _gr_gauges_maintain_type(ts, GAUGE_STREAMS, pool));

finish:
  iwpool_destroy(pool);
  return rc;
}
