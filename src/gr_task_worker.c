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

#include "gr_task_worker.h"

#include "acme/acme.h"
#include "gr_gauges.h"
#include "lic_env.h"

#include <iwnet/iwn_scheduler.h>

#include <ejdb2/ejdb2.h>
#include <ejdb2/iowow/iwp.h>
#include <ejdb2/iowow/iwstw.h>
#include <ejdb2/iowow/iwarr.h>

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

static IWSTW _stw;

static void _on_timeout();
static iwrc _do_persistent_tasks();

static void _next_run() {
  iwrc rc = iwn_schedule(&(struct iwn_scheduler_spec) {
    .poller = g_env.poller,
    .task_fn = _on_timeout,
    .timeout_ms = g_env.periodic_worker.check_timeout_sec * 1000,
  });
  if (rc) {
    iwlog_ecode_error3(rc);
    gr_shutdown();
  }
}

static void _on_timeout(void *arg) {
  iwrc rc = 0;
  JQL q = 0;
  uint64_t ts;
  RCC(rc, finish, iwp_current_time_ms(&ts, false));

  int64_t timeout = (int64_t) g_env.periodic_worker.expire_guest_session_timeout_sec * 1000;
  if ((timeout > 0) && (timeout < ts)) {
    RCC(rc, finish, jql_create(&q, "sessions", "/[__ts__ <= :?] and /[__perms__ = :?] | del"));
    RCC(rc, finish, jql_set_i64(q, 0, 0, ts - timeout));
    RCC(rc, finish, jql_set_str(q, 0, 1, "guest"));
    rc = ejdb_update(g_env.db, q);
    if (rc) {
      iwlog_ecode_error3(rc);
    }

    jql_destroy(&q);
    RCC(rc, finish, jql_create(&q, "users", "/[ctime <= :?] and /[guest = :?] | del"));
    RCC(rc, finish, jql_set_i64(q, 0, 0, ts - timeout));
    RCC(rc, finish, jql_set_bool(q, 0, 1, true));
    rc = ejdb_update(g_env.db, q);
    if (rc) {
      iwlog_ecode_error3(rc);
    }
  }

  timeout = (int64_t) g_env.periodic_worker.expire_session_timeout_sec * 1000;
  if ((timeout > 0) && (timeout < ts)) {
    jql_destroy(&q);
    RCC(rc, finish, jql_create(&q, "sessions", "/[__ts__ <= :?] | del"));
    RCC(rc, finish, jql_set_i64(q, 0, 0, ts - timeout));
    rc = ejdb_update(g_env.db, q);
    if (rc) {
      iwlog_ecode_error3(rc);
    }
  }

  timeout = (int64_t) g_env.periodic_worker.expire_ws_ticket_timeout_sec * 1000;
  if ((timeout > 0) && (timeout < ts)) {
    jql_destroy(&q);
    RCC(rc, finish, jql_create(&q, "tickets", "/[ts <= :?] | del"));
    RCC(rc, finish, jql_set_i64(q, 0, 0, ts - timeout));
    rc = ejdb_update(g_env.db, q);
    if (rc) {
      iwlog_ecode_error3(rc);
    }
  }

  if (g_env.periodic_worker.dispose_gauges_older_than_sec > 0) {
    // Gleanup old gauges
    int64_t t = ts / 1000 - g_env.periodic_worker.dispose_gauges_older_than_sec;
    jql_destroy(&q);
    RCC(rc, finish, jql_create(&q, "gauges", "/[t < :?] | del"));
    RCC(rc, finish, jql_set_i64(q, 0, 0, t));
    rc = ejdb_update(g_env.db, q);
    if (rc) {
      iwlog_ecode_error3(rc);
    }
  }

  timeout = (int64_t) g_env.whiteboard.room_data_ttl_sec * 1000;
  if ((timeout > 0) && (timeout < ts)) {
    jql_destroy(&q);
    RCC(rc, finish, jql_create(&q, "whiteboards", "/[ctime <= :?] | del"));
    RCC(rc, finish, jql_set_i64(q, 0, 0, ts - timeout));
    rc = ejdb_update(g_env.db, q);
    if (rc) {
      iwlog_ecode_error3(rc);
    }
  }

  RCC(rc, finish, gr_gauges_maintain());

  if (g_env.domain_name) {
    acme_sync_consumer_detached(0, acme_consumer_callback);
  }

  _do_persistent_tasks();

finish:
  jql_destroy(&q);
  _next_run();
}

///////////////////////////////////////////////////////////////////////////
//		                    Persistent tasks                               //
///////////////////////////////////////////////////////////////////////////

#if (ENABLE_RECORDING == 1)
#include "rec/recording_postproc.h"
#endif

static iwrc _pt_task_visitor(EJDB_EXEC *exec, EJDB_DOC doc, int64_t *step) {
  iwrc rc = 0;
  int64_t status = 0;
  IWULIST *pending = exec->opaque;
  RCC(rc, finish, jbl_object_get_i64(doc->raw, "status", &status));
  if (status) { // We have reached a one of completed task
    *step = 0;
    goto finish;
  }
  iwulist_push(pending, &doc->id);

finish:
  if (rc) {
    iwlog_ecode_error3(rc);
    *step = 0;
  }
  return 0;
}

static int(*_pt_handler_get(int type))(int64_t, JBL) {
  // Hardcodec task handlers
  switch (type) {
#if (ENABLE_RECORDING == 1)
    case PT_RECORDING_POSTPROC:
      return recording_postproc;
#endif
  }
  return 0;
}

static void _pt_task_update(int64_t id, int status, const char *log, iwrc rc_status) {
  JBL_NODE n;
  iwrc rc = 0;
  IWPOOL *pool = iwpool_create_empty();
  RCA(pool, finish);
  if (rc_status) {
    log = iwlog_ecode_explained(rc_status);
  }
  RCC(rc, finish, jbn_from_json("{}", &n, pool));
  RCC(rc, finish, jbn_add_item_i64(n, "status", status, 0, pool));
  if (log) {
    RCC(rc, finish, jbn_add_item_str(n, "log", log, -1, 0, pool));
  }
  RCC(rc, finish, ejdb_patch_jbn(g_env.db, "tasks", n, id));

finish:
  iwpool_destroy(pool);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static void _persistent_tasks_worker(void *arg) {
  JQL q = 0;
  IWULIST pending = { 0 };
  iwrc rc = jql_create(&q, "tasks", "/*");
  EJDB_EXEC ux = {
    .db      = g_env.db,
    .q       = q,
    .opaque  = &pending,
    .visitor = _pt_task_visitor
  };
  RCGO(rc, finish);
  RCC(rc, finish, iwulist_init(&pending, 32, sizeof(int64_t)));

  while (pending.num == 0) {
    RCC(rc, finish, ejdb_exec(&ux));
    if (pending.num == 0) {
      break;
    }
    while (pending.num) {
      int status = 0;
      JBL doc = 0;
      int64_t type;
      int64_t id = *(int64_t*) iwulist_at2(&pending, pending.num - 1);
      iwulist_pop(&pending);

      if (g_env.shutdown) {
        goto finish;
      }

      rc = ejdb_get(g_env.db, "tasks", id, &doc);
      if (rc) {
        continue;
      }
      rc = jbl_object_get_i64(doc, "type", &type);
      if (rc) {
        _pt_task_update(id, -1, "Invalid task data", rc);
        jbl_destroy(&doc);
        continue;
      }
      int (*handler)(int64_t, JBL) = _pt_handler_get(type);
      if (handler) {
        status = handler(id, doc);
      }
      jbl_destroy(&doc);

      if (!handler) {
        iwlog_warn("Unsupported/disabled task of type %" PRId64 " task: %" PRId64, type, id);
        _pt_task_update(id, -1, 0, rc); // Mark task failed in advance
        continue;
      }
      if (status == 0) {
        iwlog_warn("Tasks processing postponed by handler, task id: %" PRId64, id);
        goto finish;
      }
      _pt_task_update(id, status, 0, 0);
    }
  }

finish:
  iwulist_destroy_keep(&pending);
  jql_destroy(&q);
  if (rc) {
    iwlog_ecode_error3(rc);
  }
}

static iwrc _do_persistent_tasks(void) {
  if (!_stw || g_env.shutdown) {
    return 0;
  }
  bool done;
  return iwstw_schedule_empty_only(_stw, _persistent_tasks_worker, 0, &done);
}

iwrc gr_persistent_task_submit(int type, const char *hook, JBL spec) {
  if (!spec) {
    return IW_ERROR_INVALID_ARGS;
  }
  int (*handler)(int64_t, JBL) = _pt_handler_get(type);
  if (!handler) {
    iwlog_warn("Unsupported task of type %d", type);
    return 0;
  }

  int64_t id;
  uint64_t ts;
  JBL jbl = 0;

  iwrc rc = RCR(iwp_current_time_ms(&ts, false));

  RCC(rc, finish, jbl_create_empty_object(&jbl));
  RCC(rc, finish, jbl_set_int64(jbl, "ts", ts));
  RCC(rc, finish, jbl_set_int64(jbl, "status", 0));
  RCC(rc, finish, jbl_set_int64(jbl, "type", type));
  RCC(rc, finish, jbl_set_string(jbl, "hook", hook));
  RCC(rc, finish, jbl_set_nested(jbl, "spec", spec));

  RCC(rc, finish, ejdb_put_new(g_env.db, "tasks", jbl, &id));

  _do_persistent_tasks();

finish:
  jbl_destroy(&jbl);
  return rc;
}

static void _shutdown(void *data) {
  iwstw_shutdown(&_stw, true);
}

iwrc gr_periodic_worker_init(void) {
  RCR(iwstw_start("grtw", 10000, false, &_stw));
  gr_shutdown_hook_add(_shutdown, 0);
  _on_timeout(0);
  return 0;
}
